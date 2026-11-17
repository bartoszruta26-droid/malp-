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

void testStrictHttpsCdnAllowlist() {
    require(yoinks::HttpUpstreamStream::isAllowedResolvedUrl(
                    "https://r1---sn.googlevideo.com/videoplayback?expire=123"),
            "Google video CDN HTTPS URL should be accepted");
    require(!yoinks::HttpUpstreamStream::isAllowedResolvedUrl(
                    "http://r1---sn.googlevideo.com/videoplayback"),
            "plain HTTP must be rejected");
    require(!yoinks::HttpUpstreamStream::isAllowedResolvedUrl(
                    "https://googlevideo.com.evil.example/videoplayback"),
            "lookalike CDN domain must be rejected");
    require(!yoinks::HttpUpstreamStream::isAllowedResolvedUrl(
                    "https://example.com/audio"),
            "arbitrary HTTPS host must be rejected");
    require(!yoinks::HttpUpstreamStream::isAllowedResolvedUrl(
                    "https://r1---sn.googlevideo.com:8443/videoplayback"),
            "non-standard HTTPS port must be rejected");
    require(!yoinks::HttpUpstreamStream::isAllowedResolvedUrl(
                    "https://user:password@r1---sn.googlevideo.com/videoplayback"),
            "URL userinfo must be rejected");
}

void testInvalidStartFailsClosedAndCannotRestart() {
    yoinks::HttpUpstreamStream stream;
    require(!stream.start("http://example.com/audio"),
            "invalid URL must fail before network activity");
    require(stream.state() == yoinks::UpstreamState::failed,
            "invalid URL must move stream into failed state");
    require(stream.error_code() == "invalid_upstream_url",
            "invalid URL must use a non-sensitive error code");
    require(!stream.start("https://r1---sn.googlevideo.com/videoplayback"),
            "a terminal stream instance must not be restarted");
    require(stream.bytes_received() == 0,
            "invalid URL must not transfer any bytes");
}

}  // namespace

int main() {
    try {
        testStrictHttpsCdnAllowlist();
        testInvalidStartFailsClosedAndCannotRestart();
        std::cout << "All HTTP upstream validation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
