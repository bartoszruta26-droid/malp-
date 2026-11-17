/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "yoinks/lazy_stream_session_registry.hpp"
#include "yoinks/yt_dlp_resolver.hpp"
#include "yoinks/youtube_queue_service.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace yoinks {

struct HttpServerConfig {
    std::string bind_address{"127.0.0.1"};
    std::uint16_t port{8788};
    std::string bearer_token;
    std::shared_ptr<YtDlpResolver> resolver;
    std::shared_ptr<LazyStreamSessionRegistry> sessions;
    std::shared_ptr<YoutubeQueueService> queue_service;
};

class HttpServer final {
public:
    explicit HttpServer(HttpServerConfig config);
    void run();

private:
    HttpServerConfig config_;
};

}  // namespace yoinks
