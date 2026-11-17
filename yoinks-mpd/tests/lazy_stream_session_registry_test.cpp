/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/lazy_stream_session_registry.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }

void testRegistrationDoesNotResolveOrStartMedia() {
    yoinks::LazyStreamSessionRegistry registry;
    std::atomic<unsigned> calls{0};
    const auto id = registry.create("qqAYW7uxErI");
    require(id.size() == 32, "valid video ID should receive opaque session capability");
    require(registry.size() == 1 && registry.activeCount() == 0, "registration must remain lazy");
    require(registry.contains(id) && !registry.isFailed(id), "registered session must be discoverable and healthy");
    require(!registry.findActive(id), "registered session must not contain an active upstream");
    require(calls.load() == 0, "registration must not invoke resolver");
}

void testInvalidVideoIdsAreRejected() {
    yoinks::LazyStreamSessionRegistry registry;
    require(registry.create("not-a-video-id").empty(), "invalid video ID must be rejected");
    require(registry.size() == 0, "invalid ID must not consume a session slot");
}

void testActivationResolvesOnlyOnDemandAndRejectsUntrustedUrl() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    unsigned calls = 0;
    const bool activated = registry.activate(id, [&](const std::string& video_id, std::string& url, const std::atomic_bool*) {
        ++calls;
        require(video_id == "qqAYW7uxErI", "resolver receives only stored video ID");
        url = "https://attacker.example/audio";
        return true;
    });
    require(!activated, "non-googlevideo URL must never start an upstream");
    require(calls == 1, "resolver should run exactly when activation is requested");
    require(registry.activeCount() == 0 && !registry.findActive(id),
            "failed activation must not retain an active stream");
    require(registry.contains(id) && registry.isFailed(id),
            "resolver failure must remain distinguishable from an unknown session");
    const bool retried = registry.activate(id, [&](const std::string&, std::string& url, const std::atomic_bool*) {
        ++calls;
        url = "https://attacker.example/retry";
        return true;
    });
    require(!retried && calls == 2 && registry.isFailed(id),
            "failed lazy sessions must permit a fresh resolver attempt on reconnect");
}

void testConcurrentActivationIsSingleFlight() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    std::atomic<unsigned> calls{0};
    auto resolver = [&](const std::string&, std::string& url, const std::atomic_bool*) {
        ++calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        url = "https://attacker.example/audio";
        return true;
    };
    bool first = true;
    bool second = true;
    std::thread a([&] { first = registry.activate(id, resolver); });
    std::thread b([&] { second = registry.activate(id, resolver); });
    a.join(); b.join();
    require(!first && !second, "untrusted URL must fail both callers");
    require(calls.load() == 1, "concurrent GETs must not duplicate resolver work");
}


void testEraseCancelsResolverInFlight() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    std::atomic<bool> resolver_entered{false};
    std::atomic<bool> resolver_observed_cancel{false};
    bool activated = true;
    std::thread worker([&] {
        activated = registry.activate(id, [&](const std::string&, std::string&, const std::atomic_bool* cancellation) {
            resolver_entered.store(true);
            while (!cancellation->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            resolver_observed_cancel.store(true);
            return false;
        });
    });
    while (!resolver_entered.load()) std::this_thread::yield();
    require(registry.erase(id), "erase should remove an activating session");
    worker.join();
    require(!activated && resolver_observed_cancel.load(),
            "erase must signal cancellation to the in-flight resolver");
    require(registry.size() == 0 && registry.activeCount() == 0,
            "cancelled resolver must not recreate an erased session");
    require(!registry.contains(id) && !registry.isFailed(id),
            "erased sessions must no longer be discoverable");
}
void testRetainOnlyPreservesInFlightActivation() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    std::mutex mutex;
    std::condition_variable condition;
    bool resolver_entered = false;
    bool allow_resolver_to_finish = false;
    bool activated = true;

    std::thread worker([&] {
        activated = registry.activate(id,
            [&](const std::string&, std::string&, const std::atomic_bool*) {
                std::unique_lock<std::mutex> lock(mutex);
                resolver_entered = true;
                condition.notify_all();
                condition.wait(lock, [&] { return allow_resolver_to_finish; });
                return false;
            });
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return resolver_entered; });
    }

    const auto removed = registry.retainOnly({});
    const bool preserved = registry.contains(id) && registry.activeCount() == 1;

    {
        std::lock_guard<std::mutex> lock(mutex);
        allow_resolver_to_finish = true;
    }
    condition.notify_all();
    worker.join();

    require(removed == 0 && preserved,
            "queue reconciliation must preserve a session while its resolver is in flight");
    require(!activated && registry.contains(id) && registry.isFailed(id),
            "a completed failed resolver must leave the retained capability available for retry");
}

void testReleaseRetainsQueuedCapabilityForReplay() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    require(registry.release(id, true), "release should relinquish a stream reader");
    require(registry.contains(id) && registry.size() == 1,
            "releasing a stream must retain its queue capability for replay/reconnect");
    require(registry.activeCount() == 0,
            "releasing an inactive session must not consume an upstream slot");
}

void testRetainOnlyPrunesSessionsRemovedFromMpdQueue() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto queued = registry.create("qqAYW7uxErI");
    const auto stale = registry.create("dQw4w9WgXcQ");
    const std::unordered_set<std::string> live{queued};
    const auto removed = registry.retainOnly(live);
    require(removed == 1 && registry.size() == 1,
            "reconciliation should remove only sessions absent from MPD");
    require(registry.contains(queued) && !registry.contains(stale),
            "live MPD capabilities must remain while stale capabilities are removed");
}

void testSessionAndBufferLimitsAreValidated() {
    yoinks::LazyStreamSessionRegistryConfig config;
    config.maximum_active_streams = 2;
    config.upstream.buffer_capacity = 4 * 1024 * 1024;
    yoinks::LazyStreamSessionRegistry allowed(config);
    config.upstream.buffer_capacity = 4 * 1024 * 1024 + 1;
    bool rejected = false;
    try { yoinks::LazyStreamSessionRegistry invalid(config); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "aggregate active RAM buffer limit must be enforced");
}

}  // namespace

int main() {
    try {
        testRegistrationDoesNotResolveOrStartMedia();
        testInvalidVideoIdsAreRejected();
        testActivationResolvesOnlyOnDemandAndRejectsUntrustedUrl();
        testConcurrentActivationIsSingleFlight();
        testEraseCancelsResolverInFlight();
        testRetainOnlyPreservesInFlightActivation();
        testReleaseRetainsQueuedCapabilityForReplay();
        testRetainOnlyPrunesSessionsRemovedFromMpdQueue();
        testSessionAndBufferLimitsAreValidated();
        std::cout << "All lazy stream session tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}