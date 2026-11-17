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
void testReleaseCancelsInFlightActivationAndAllowsFreshRetry() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    std::mutex mutex;
    std::condition_variable condition;
    bool resolver_entered = false;
    bool allow_resolver_to_finish = false;
    bool resolver_observed_cancel = false;
    bool activated = true;

    std::thread worker([&] {
        activated = registry.activate(id,
            [&](const std::string&, std::string&, const std::atomic_bool* cancellation) {
                std::unique_lock<std::mutex> lock(mutex);
                resolver_entered = true;
                condition.notify_all();
                while (!cancellation->load()) {
                    condition.wait_for(lock, std::chrono::milliseconds(1));
                }
                resolver_observed_cancel = true;
                condition.notify_all();
                condition.wait(lock, [&] { return allow_resolver_to_finish; });
                return false;
            });
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return resolver_entered; });
    }

    require(registry.release(id, true),
            "release should retain the queued capability and cancel its in-flight activation");

    bool observed_cancel = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return resolver_observed_cancel; });
        observed_cancel = resolver_observed_cancel;
    }
    require(observed_cancel, "release must signal the active resolver cancellation token");

    const bool overlapping_activation = registry.activate(
        id, [](const std::string&, std::string&, const std::atomic_bool*) { return false; });
    require(!overlapping_activation,
            "release must not clear the activation reservation while the resolver is still running");

    {
        std::lock_guard<std::mutex> lock(mutex);
        allow_resolver_to_finish = true;
    }
    condition.notify_all();
    worker.join();

    require(!activated && registry.contains(id),
            "cancelled activation must finish without deleting the queued capability");
    require(registry.activeCount() == 0,
            "completed release cancellation must free the active slot");

    bool retry_token_was_fresh = false;
    const bool retried = registry.activate(
        id, [&](const std::string&, std::string&, const std::atomic_bool* cancellation) {
            retry_token_was_fresh = cancellation && !cancellation->load();
            return false;
        });
    require(!retried && retry_token_was_fresh,
            "a later explicit retry must receive a fresh, uncancelled token");
}

void testReleaseDuringStartedButUnpublishedStreamFreesReservation() {
    std::mutex mutex;
    std::condition_variable condition;
    bool after_start_entered = false;
    bool allow_publication_check = false;
    bool pause_once = true;

    yoinks::LazyStreamSessionRegistryConfig config;
    config.upstream.connect_timeout_seconds = 1;
    config.upstream.test_connect_to_for_tests =
            "rr1.googlevideo.com:443:127.0.0.1:1";
    config.after_upstream_start_for_tests = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        if (!pause_once) return;
        pause_once = false;
        after_start_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return allow_publication_check; });
    };
    yoinks::LazyStreamSessionRegistry registry(config);
    const auto id = registry.create("qqAYW7uxErI");
    bool activated = true;

    std::thread worker([&] {
        activated = registry.activate(id,
            [](const std::string&, std::string& url, const std::atomic_bool*) {
                url = "https://rr1.googlevideo.com/audio.bin";
                return true;
            });
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return after_start_entered; });
    }

    require(registry.contains(id) && registry.activeCount() == 1
                    && !registry.findActive(id),
            "started upstream must retain its activation reservation until publication");

    require(registry.release(id, true),
            "release must cancel an upstream that has started but is not yet published");

    const bool overlapping_activation = registry.activate(
        id, [](const std::string&, std::string&, const std::atomic_bool*) {
            return false;
        });
    require(!overlapping_activation,
            "a started but unpublished upstream must continue to own the activation slot");

    {
        std::lock_guard<std::mutex> lock(mutex);
        allow_publication_check = true;
    }
    condition.notify_all();
    worker.join();

    require(!activated && registry.contains(id) && !registry.findActive(id),
            "cancelled unpublished upstream must be discarded without deleting its capability");
    require(registry.activeCount() == 0 && !registry.isFailed(id),
            "discarding the cancelled upstream must release the reservation without marking failure");

    const bool retried = registry.activate(id,
        [](const std::string&, std::string& url, const std::atomic_bool*) {
            url = "https://rr1.googlevideo.com/audio.bin";
            return true;
        });
    require(retried,
            "a later explicit activation must succeed after the discarded worker has been joined");
    require(registry.release(id, true),
            "the retry stream must remain releasable for deterministic test cleanup");
    require(registry.activeCount() == 0,
            "cleanup after retry must leave no active stream reservation");
}

void testEraseDuringStartedButUnpublishedStreamDoesNotRecreateSession() {
    std::mutex mutex;
    std::condition_variable condition;
    bool after_start_entered = false;
    bool allow_discard = false;

    yoinks::LazyStreamSessionRegistryConfig config;
    config.upstream.connect_timeout_seconds = 1;
    config.upstream.test_connect_to_for_tests =
            "rr1.googlevideo.com:443:127.0.0.1:1";
    config.after_upstream_start_for_tests = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        after_start_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return allow_discard; });
    };
    yoinks::LazyStreamSessionRegistry registry(config);
    const auto id = registry.create("qqAYW7uxErI");
    bool activated = true;

    std::thread worker([&] {
        activated = registry.activate(id,
            [](const std::string&, std::string& url, const std::atomic_bool*) {
                url = "https://rr1.googlevideo.com/audio.bin";
                return true;
            });
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return after_start_entered; });
    }

    require(registry.erase(id),
            "erase must remove a session whose upstream has started but is unpublished");
    require(!registry.contains(id) && registry.activeCount() == 0,
            "erase must immediately remove the unpublished session reservation");

    {
        std::lock_guard<std::mutex> lock(mutex);
        allow_discard = true;
    }
    condition.notify_all();
    worker.join();

    require(!activated && registry.size() == 0 && registry.activeCount() == 0,
            "the cancelled unpublished activation must finish without recreating an erased session");
}

void testClearDuringStartedButUnpublishedStreamDoesNotRecreateSession() {
    std::mutex mutex;
    std::condition_variable condition;
    bool after_start_entered = false;
    bool allow_discard = false;

    yoinks::LazyStreamSessionRegistryConfig config;
    config.upstream.connect_timeout_seconds = 1;
    config.upstream.test_connect_to_for_tests =
            "rr1.googlevideo.com:443:127.0.0.1:1";
    config.after_upstream_start_for_tests = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        after_start_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return allow_discard; });
    };
    yoinks::LazyStreamSessionRegistry registry(config);
    const auto id = registry.create("qqAYW7uxErI");
    bool activated = true;

    std::thread worker([&] {
        activated = registry.activate(id,
            [](const std::string&, std::string& url, const std::atomic_bool*) {
                url = "https://rr1.googlevideo.com/audio.bin";
                return true;
            });
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return after_start_entered; });
    }

    registry.clear();
    require(registry.size() == 0 && registry.activeCount() == 0,
            "clear must remove the session whose upstream has started but is unpublished");

    {
        std::lock_guard<std::mutex> lock(mutex);
        allow_discard = true;
    }
    condition.notify_all();
    worker.join();

    require(!activated && registry.size() == 0 && registry.activeCount() == 0,
            "the cancelled unpublished activation must finish without recreating a cleared session");
}

void testRetainOnlyPreservesUnpublishedActivationAndSlotAccounting() {
    std::mutex mutex;
    std::condition_variable condition;
    bool after_start_entered = false;
    bool allow_publication = false;

    yoinks::LazyStreamSessionRegistryConfig config;
    config.maximum_active_streams = 1;
    config.upstream.connect_timeout_seconds = 1;
    config.upstream.test_connect_to_for_tests =
            "rr1.googlevideo.com:443:127.0.0.1:1";
    config.after_upstream_start_for_tests = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        after_start_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return allow_publication; });
    };
    yoinks::LazyStreamSessionRegistry registry(config);
    const auto first_id = registry.create("qqAYW7uxErI");
    const auto second_id = registry.create("dQw4w9WgXcQ");
    require(!first_id.empty() && !second_id.empty(),
            "both test capabilities must be created");

    bool first_activated = false;
    std::thread worker([&] {
        first_activated = registry.activate(first_id,
            [](const std::string&, std::string& url, const std::atomic_bool*) {
                url = "https://rr1.googlevideo.com/audio.bin";
                return true;
            });
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return after_start_entered; });
    }

    // Record every assertion while the activation is paused, but always release
    // and join the worker before throwing. Otherwise a failed require() would
    // destroy a joinable std::thread and mask the actual assertion with terminate.
    const bool unpublished_slot_reserved =
            registry.activeCount() == 1 && registry.contains(first_id);
    const auto removed = registry.retainOnly({});
    const bool retain_only_preserved =
            registry.contains(first_id) && registry.activeCount() == 1;
    const bool second_activated_early = registry.activate(second_id,
        [](const std::string&, std::string&, const std::atomic_bool*) {
            return false;
        });

    {
        std::lock_guard<std::mutex> lock(mutex);
        allow_publication = true;
    }
    condition.notify_all();
    worker.join();

    require(unpublished_slot_reserved,
            "the unpublished upstream must own exactly one active slot");
    require(removed == 0,
            "retainOnly must preserve an in-flight unpublished activation");
    require(retain_only_preserved,
            "the retained unpublished activation must keep its capability and slot");
    require(!second_activated_early,
            "a second session must not exceed the single active-stream slot");
    require(first_activated && registry.activeCount() == 1,
            "publishing the retained upstream must preserve one active slot");
    require(registry.release(first_id, true),
            "the first stream must be releasable before retrying the second session");
    require(registry.activeCount() == 0,
            "releasing the first stream must free the active slot");

    const bool second_activated = registry.activate(second_id,
        [](const std::string&, std::string& url, const std::atomic_bool*) {
            url = "https://rr1.googlevideo.com/audio.bin";
            return true;
        });
    require(second_activated,
            "the second session must activate after the first stream releases its slot");
    require(registry.release(second_id, true),
            "the second stream must be releasable for deterministic cleanup");
    require(registry.activeCount() == 0,
            "cleanup must leave no active stream slots");
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
        testReleaseCancelsInFlightActivationAndAllowsFreshRetry();
        testReleaseDuringStartedButUnpublishedStreamFreesReservation();
        testEraseDuringStartedButUnpublishedStreamDoesNotRecreateSession();
        testClearDuringStartedButUnpublishedStreamDoesNotRecreateSession();
        testRetainOnlyPreservesUnpublishedActivationAndSlotAccounting();
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