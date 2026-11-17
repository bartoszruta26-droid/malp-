/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace yoinks {

struct MpdPlaylistWatchConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{6600};
    std::uint32_t operation_timeout_milliseconds{30000};
};

struct MpdPlaylistWatchResult {
    bool success{false};
    bool playlist_changed{false};
    bool timed_out{false};
    std::string error_code;
};

/**
 * One-shot MPD idle subscription. Use a dedicated connection for this call;
 * never share it with command/snapshot connections. The caller owns retry and
 * lifecycle policy, and should fetch a fresh playlist snapshot after a change.
 */
class MpdPlaylistWatcher final {
public:
    explicit MpdPlaylistWatcher(MpdPlaylistWatchConfig config = {});
    MpdPlaylistWatchResult waitForPlaylistChange() const;

private:
    MpdPlaylistWatchConfig config_;
};

}  // namespace yoinks
