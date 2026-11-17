/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/youtube_queue_service.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace yoinks {

YoutubeQueueService::YoutubeQueueService(YoutubeQueueServiceConfig config)
    : config_(std::move(config)) {
    if (!config_.sessions || !config_.resolve_metadata || !config_.enqueue
            || config_.stream_port == 0 || config_.maximum_entries == 0
            || config_.maximum_entries > 200) {
        throw std::invalid_argument("invalid YouTube queue service configuration");
    }
}

YoutubeQueueServiceResult YoutubeQueueService::enqueue(
        const std::string& youtube_url, bool play_now) const {
    YoutubeQueueServiceResult result;
    MetadataResult metadata;
    try {
        metadata = config_.resolve_metadata(youtube_url);
    } catch (...) {
        result.error_code = "metadata_resolution_failed";
        return result;
    }
    if (!metadata.success) {
        result.error_code = metadata.error_code.empty()
                ? "metadata_resolution_failed" : metadata.error_code;
        return result;
    }
    if (metadata.entries.empty() || metadata.entries.size() > config_.maximum_entries) {
        result.error_code = "invalid_entry_count";
        return result;
    }

    if (config_.queued_session_ids) {
        std::vector<std::string> queued_ids;
        bool snapshot_ok = false;
        try { snapshot_ok = config_.queued_session_ids(queued_ids); } catch (...) { snapshot_ok = false; }
        if (!snapshot_ok) {
            result.error_code = "mpd_reconcile_failed";
            return result;
        }
        const std::unordered_set<std::string> live_ids(queued_ids.begin(), queued_ids.end());
        config_.sessions->retainOnly(live_ids);
    }

    std::vector<std::string> session_ids;
    std::vector<std::string> stream_uris;
    session_ids.reserve(metadata.entries.size());
    stream_uris.reserve(metadata.entries.size());
    const std::string prefix = "http://127.0.0.1:"
            + std::to_string(config_.stream_port) + "/stream/";
    for (const auto& entry : metadata.entries) {
        const std::string id = config_.sessions->create(entry.video_id);
        if (id.empty()) {
            for (const auto& created : session_ids) config_.sessions->erase(created);
            result.error_code = "session_capacity_reached";
            return result;
        }
        session_ids.push_back(id);
        stream_uris.push_back(prefix + id);
    }

    MpdQueueResult queued;
    try {
        queued = config_.enqueue(stream_uris, play_now);
    } catch (...) {
        // A thrown transport error has an unknown MPD outcome. Keep capabilities
        // alive so an item MPD may already have accepted does not become a dead URI.
        result.error_code = "mpd_outcome_unknown";
        result.rollback_complete = false;
        return result;
    }
    if (!queued.success) {
        result.error_code = queued.error_code.empty() ? "mpd_enqueue_failed" : queued.error_code;
        result.rollback_complete = queued.rollback_complete;
        result.queued = queued.queued;
        if (queued.rollback_complete) {
            for (const auto& created : session_ids) config_.sessions->erase(created);
        }
        return result;
    }
    result.success = true;
    result.started = play_now;
    result.queued = queued.queued;
    return result;
}

}  // namespace yoinks
