/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "yoinks/bounded_stream_buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace yoinks {

struct HttpUpstreamConfig {
    std::size_t buffer_capacity{256 * 1024};
    std::uint64_t maximum_bytes{512ULL * 1024ULL * 1024ULL};
    long connect_timeout_seconds{10};
    long low_speed_limit_bytes_per_second{1024};
    long low_speed_time_seconds{30};
    // Optional test-only TLS fixture routing; production leaves both fields empty.
    std::string test_ca_bundle_path_for_tests;
    std::string test_connect_to_for_tests;
};

enum class UpstreamState {
    idle,
    running,
    completed,
    cancelled,
    failed
};

/**
 * Pulls one HTTPS media response into a fixed-size RAM buffer.
 *
 * libcurl performs TLS peer/host verification and streams callback chunks
 * directly into BoundedStreamBuffer. Redirects are disabled. No media
 * file, temporary file, FIFO, disk cache or unbounded response body is used.
 * The URL must come from the trusted resolver, never directly from Android.
 */
class HttpUpstreamStream final {
public:
    explicit HttpUpstreamStream(HttpUpstreamConfig config = {});
    ~HttpUpstreamStream();

    HttpUpstreamStream(const HttpUpstreamStream&) = delete;
    HttpUpstreamStream& operator=(const HttpUpstreamStream&) = delete;

    // Starts exactly one upstream request. Only HTTPS URLs without userinfo
    // are accepted. The source URL is never included in error messages.
    bool start(const std::string& resolved_https_url);

    // Exposed for deterministic security tests; accepts only trusted media CDN URLs.
    static bool isAllowedResolvedUrl(const std::string& value);

    // Progressive consumer interface; returns 0 at EOF/cancellation.
    std::size_t read(std::uint8_t* destination, std::size_t maximum);

    // Cancels network transfer and wakes a producer blocked by RAM back-pressure.
    void cancel();

    // Joins the producer thread. Safe to call more than once.
    void wait();

    UpstreamState state() const noexcept;
    std::string error_code() const;
    std::uint64_t bytes_received() const noexcept;

private:
    static std::size_t writeCallback(char* data, std::size_t size,
                                     std::size_t count, void* context);
    static int progressCallback(void* context, long download_total,
                                long download_now, long upload_total,
                                long upload_now);
    void run(std::string url);

    HttpUpstreamConfig config_;
    BoundedStreamBuffer buffer_;
    std::thread worker_;
    std::atomic<bool> cancelled_{false};
    std::atomic<UpstreamState> state_{UpstreamState::idle};
    std::atomic<std::uint64_t> bytes_received_{0};
    mutable std::mutex error_mutex_;
    std::string error_code_;
};

}  // namespace yoinks
