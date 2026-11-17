/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

namespace yoinks {

struct YtDlpResolverConfig {
    std::string executable{ "/usr/local/bin/yt-dlp" };
    std::uint32_t timeout_milliseconds{30000};
    std::size_t maximum_stdout_bytes{1024 * 1024};
    std::size_t maximum_stderr_bytes{64 * 1024};
    std::size_t maximum_playlist_entries{200};
};

struct ResolvedMediaEntry {
    std::string video_id;
    std::string title;
    double duration_seconds{0.0};
};

struct MetadataResult {
    bool success{false};
    std::string error_code;
    std::vector<ResolvedMediaEntry> entries;
};

/**
 * Runs yt-dlp with an argv vector (never through a shell). Metadata and
 * direct-URL resolution both disable downloads and cache. stdout/stderr
 * are bounded, execution is timed, and child process groups are killed on
 * timeout or output-limit violations. Sensitive output is never logged.
 */
class YtDlpResolver final {
public:
    explicit YtDlpResolver(YtDlpResolverConfig config = {});

    // A caller-owned flag may be set from another thread to terminate yt-dlp.
    // The flag must outlive the synchronous resolver call.
    MetadataResult resolveMetadata(const std::string& youtube_url,
                                    const std::atomic_bool* cancellation = nullptr) const;
    // Returns a validated googlevideo HTTPS URL in memory only.
    bool resolveAudioUrl(const std::string& video_id, std::string& output_url,
                         const std::atomic_bool* cancellation = nullptr) const;

    static bool isValidVideoId(const std::string& video_id) noexcept;

private:
    YtDlpResolverConfig config_;
};

}  // namespace yoinks
