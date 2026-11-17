/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_server.hpp"
#include "yoinks/mpd_queue_manager.hpp"

#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <functional>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string environment(const char* name, const std::string& fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr ? fallback : value;
}

std::uint16_t portFromEnvironment(const char* name) {
    const std::string value = environment(name);
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed);
    if (consumed != value.size() || parsed == 0
            || parsed > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument(std::string(name) + " must be a valid TCP port");
    }
    return static_cast<std::uint16_t>(parsed);
}

}  // namespace

int main() {
    try {
        yoinks::HttpServerConfig config;
        config.port = portFromEnvironment("YOINKS_MPD_PORT");
        config.bearer_token = environment("YOINKS_MPD_TOKEN");

        yoinks::YtDlpResolverConfig resolver_config;
        resolver_config.executable = environment("YOINKS_MPD_YTDLP");
        config.resolver = std::make_shared<yoinks::YtDlpResolver>(resolver_config);

        yoinks::LazyStreamSessionRegistryConfig session_config;
        session_config.upstream.test_ca_bundle_path_for_tests =
                environment("YOINKS_MPD_TEST_CA_BUNDLE");
        session_config.upstream.test_connect_to_for_tests =
                environment("YOINKS_MPD_TEST_CONNECT_TO");
        config.sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>(
                std::move(session_config));

        yoinks::MpdQueueManagerConfig mpd_config;
        mpd_config.port = portFromEnvironment("YOINKS_MPD_MPD_PORT");
        mpd_config.stream_port = config.port;
        mpd_config.operation_timeout_milliseconds = 5000;
        auto mpd = std::make_shared<yoinks::MpdQueueManager>(mpd_config);

        yoinks::YoutubeQueueServiceConfig queue_config;
        queue_config.stream_port = config.port;
        queue_config.sessions = config.sessions;
        queue_config.resolve_metadata = [resolver = config.resolver](const std::string& url) {
            return resolver->resolveMetadata(url);
        };
        queue_config.enqueue = [mpd](const std::vector<std::string>& uris, bool play_now) {
            return mpd->enqueue(uris, play_now);
        };
        queue_config.queued_session_ids = [mpd](std::vector<std::string>& ids) {
            const auto snapshot = mpd->listStreamSessionIds();
            if (!snapshot.success) return false;
            ids = snapshot.stream_session_ids;
            return true;
        };
        config.queue_service = std::make_shared<yoinks::YoutubeQueueService>(
                std::move(queue_config));

        yoinks::HttpServer server{std::move(config)};
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "HTTP stream fixture failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
