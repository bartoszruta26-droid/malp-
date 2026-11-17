/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yoinks {

struct MpdQueueManagerConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{6600};
    std::uint16_t stream_port{8788};
    std::uint32_t operation_timeout_milliseconds{30000};
    std::size_t maximum_entries{200};
};

struct MpdQueueResult {
    bool success{false};
    bool rollback_complete{true};
    std::size_t queued{0};
    std::vector<std::uint32_t> song_ids;
    std::string error_code;
};

struct MpdQueueSnapshot {
    bool success{false};
    std::vector<std::string> stream_session_ids;
    std::string error_code;
};

struct MpdPlaylistEntry {
    std::uint32_t song_id{0};
    std::size_t position{0};
    std::string uri;
};

struct MpdPlaylistStateSnapshot {
    bool success{false};
    std::vector<MpdPlaylistEntry> entries;
    std::string state;
    std::optional<std::uint32_t> current_song_id;
    std::string error_code;
};

/**
 * Appends only trusted local proxy URIs to MPD. It never clears the queue
 * or changes repeat/random/single/consume. play_now starts the first newly
 * added song only when explicitly requested.
 */
class MpdQueueManager final {
public:
    explicit MpdQueueManager(MpdQueueManagerConfig config = {});

    MpdQueueResult enqueue(const std::vector<std::string>& stream_uris,
                           bool play_now) const;
    MpdQueueSnapshot listStreamSessionIds() const;
    MpdPlaylistStateSnapshot listPlaylistState() const;

    bool isAllowedStreamUri(const std::string& uri) const;

private:
    MpdQueueManagerConfig config_;
};

}  // namespace yoinks
