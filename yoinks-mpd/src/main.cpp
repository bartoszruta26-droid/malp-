/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_server.hpp"
#include "yoinks/mpd_queue_manager.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <string>

namespace {

std::string environment(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value == nullptr ? fallback : value;
}

std::uint16_t portFromEnvironment(const char* variable = "YOINKS_MPD_PORT",
                                  const char* fallback = "8788") {
    const std::string value = environment(variable, fallback);
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

        yoinks::YtDlpResolverConfig resolver_config;
        resolver_config.executable = environment("YOINKS_MPD_YTDLP", "/usr/local/bin/yt-dlp");
        config.resolver = std::make_shared<yoinks::YtDlpResolver>(std::move(resolver_config));
        config.sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();

        yoinks::MpdQueueManagerConfig mpd_config;
        mpd_config.port = portFromEnvironment("YOINKS_MPD_MPD_PORT", "6600");
        mpd_config.stream_port = config.port;
        auto mpd = std::make_shared<yoinks::MpdQueueManager>(std::move(mpd_config));

        yoinks::YoutubeQueueServiceConfig queue_config;
        queue_config.stream_port = config.port;
        queue_config.sessions = config.sessions;
        queue_config.resolve_metadata = [resolver = config.resolver](const std::string& url) {
            return resolver->resolveMetadata(url);
        };
        queue_config.enqueue = [mpd](const std::vector<std::string>& uris, bool play_now) {
            return mpd->enqueue(uris, play_now);
        };
        config.queue_service = std::make_shared<yoinks::YoutubeQueueService>(std::move(queue_config));

        yoinks::HttpServer server{std::move(config)};
        server.run();
    } catch (const std::exception& error) {
        // Never print the configured token or request contents.
        std::cerr << "yoinks-mpd startup/runtime error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
