/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "yoinks/lazy_stream_session_registry.hpp"
#include "yoinks/mpd_queue_manager.hpp"
#include "yoinks/yt_dlp_resolver.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yoinks {

struct YoutubeQueueServiceConfig {
    std::uint16_t stream_port{8788};
    std::size_t maximum_entries{200};
    std::shared_ptr<LazyStreamSessionRegistry> sessions;
    std::function<MetadataResult(const std::string&)> resolve_metadata;
    std::function<MpdQueueResult(const std::vector<std::string>&, bool)> enqueue;
};

struct YoutubeQueueServiceResult {
    bool success{false};
    bool rollback_complete{true};
    std::size_t queued{0};
    std::string error_code;
};

/**
 * Coordinates metadata resolution, lazy RAM-only stream capabilities and MPD enqueue.
 * It never resolves CDN URLs while adding a playlist to MPD.
 */
class YoutubeQueueService final {
public:
    explicit YoutubeQueueService(YoutubeQueueServiceConfig config);
    YoutubeQueueServiceResult enqueue(const std::string& youtube_url, bool play_now) const;

private:
    YoutubeQueueServiceConfig config_;
};

}  // namespace yoinks
