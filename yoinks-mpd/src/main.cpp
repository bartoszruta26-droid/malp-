/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_server.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string>

namespace {

std::string environment(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value == nullptr ? fallback : value;
}

std::uint16_t portFromEnvironment() {
    const std::string value = environment("YOINKS_MPD_PORT", "8788");
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed);
    if (consumed != value.size() || parsed == 0
            || parsed > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("YOINKS_MPD_PORT must be an integer from 1 to 65535");
    }
    return static_cast<std::uint16_t>(parsed);
}

}  // namespace

int main() {
    try {
        yoinks::HttpServerConfig config;
        config.bind_address = environment("YOINKS_MPD_BIND", "127.0.0.1");
        config.port = portFromEnvironment();
        config.bearer_token = environment("YOINKS_MPD_TOKEN", "");
        yoinks::HttpServer server{std::move(config)};
        server.run();
    } catch (const std::exception& error) {
        // Never print the configured token or request contents.
        std::cerr << "yoinks-mpd startup/runtime error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
