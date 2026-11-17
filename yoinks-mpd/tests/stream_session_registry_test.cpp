/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/stream_session_registry.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testRejectsInvalidSourceWithoutCreatingSession() {
    yoinks::StreamSessionRegistry registry;
    require(registry.create("http://example.com/audio").empty(),
            "registry must reject non-HTTPS source");
    require(registry.size() == 0, "invalid source must not create a session");
}

void testUnknownSessionLookupAndErase() {
    yoinks::StreamSessionRegistry registry;
    require(!registry.find("00000000000000000000000000000000"),
            "unknown session must not be returned");
    require(!registry.erase("00000000000000000000000000000000"),
            "unknown session must not be erased");
    require(registry.size() == 0, "registry should remain empty");
}

void testRejectsInvalidCapacity() {
    yoinks::StreamSessionRegistryConfig config;
    config.maximum_sessions = 33;
    bool threw = false;
    try {
        yoinks::StreamSessionRegistry registry(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "registry must reject an excessive session limit");
}

}  // namespace

int main() {
    try {
        testRejectsInvalidSourceWithoutCreatingSession();
        testUnknownSessionLookupAndErase();
        testRejectsInvalidCapacity();
        std::cout << "All stream session registry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
