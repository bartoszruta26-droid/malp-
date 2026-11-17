/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/lazy_stream_session_registry.hpp"
#include <atomic>
#include <exception>
#include <iostream>
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
    require(!registry.findActive(id), "registered session must not contain an active upstream");
    require(calls.load() == 0, "registration must not invoke resolver");
}

void testInvalidVideoIdsAreRejected() {
    yoinks::LazyStreamSessionRegistry registry;
    require(registry.create("not-a-video-id") .empty(), "invalid video ID must be rejected");
    require(registry.size() == 0, "invalid ID must not consume a session slot");
}

void testActivationResolvesOnlyOnDemandAndRejectsUntrustedUrl() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    unsigned calls = 0;
    const bool activated = registry.activate(id, [&](const std::string& video_id, std::string& url) {
        ++calls;
        require(video_id == "qqAYW7uxErI", "resolver receives only stored video ID");
        url = "https://attacker.example/audio";
        return true;
    });
    require(!activated, "non-googlevideo URL must never start an upstream");
    require(calls == 1, "resolver should run exactly when activation is requested");
    require(registry.activeCount() == 0 && !registry.findActive(id),
            "failed activation must not retain an active stream");
}

void testConcurrentActivationIsSingleFlight() {
    yoinks::LazyStreamSessionRegistry registry;
    const auto id = registry.create("qqAYW7uxErI");
    std::atomic<unsigned> calls{0};
    auto resolver = [&](const std::string&, std::string& url) {
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
        testSessionAndBufferLimitsAreValidated();
        std::cout << "All lazy stream session tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}