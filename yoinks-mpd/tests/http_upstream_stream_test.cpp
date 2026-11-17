/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_upstream_stream.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testRejectsNonHttpsAndUserInfo() {
    yoinks::HttpUpstreamStream stream;
    require(!stream.start("http://example.com/audio"), "plain HTTP must be rejected");
    require(stream.state() == yoinks::UpstreamState::failed,
            "invalid URL must fail before network activity");
    require(stream.error_code() == "invalid_upstream_url",
            "invalid URL must use a non-sensitive error code");

    yoinks::HttpUpstreamStream credentials;
    require(!credentials.start("https://user:password@example.com/audio"),
            "URL userinfo must be rejected");
    require(credentials.bytes_received() == 0,
            "invalid URL must not transfer any bytes");
}

void testRejectsSecondStartAfterFailure() {
    yoinks::HttpUpstreamStream stream;
    require(!stream.start("http://example.com/audio"), "invalid URL should fail");
    require(!stream.start("https://example.com/audio"),
            "a stream instance must not be restarted after terminal failure");
}

}  // namespace

int main() {
    try {
        testRejectsNonHttpsAndUserInfo();
        std::cout << "All HTTP upstream validation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
