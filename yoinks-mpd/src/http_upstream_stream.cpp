/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_upstream_stream.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace yoinks {
namespace {

bool ensureCurlInitialized() {
    static const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    return initialized == CURLE_OK;
}

bool validResolvedUrl(const std::string& value) {
    if (!ensureCurlInitialized()) return false;
    CURLU* parsed = curl_url();
    if (parsed == nullptr) return false;
    const CURLUcode set_result = curl_url_set(parsed, CURLUPART_URL, value.c_str(), 0);
    char* scheme = nullptr;
    char* host = nullptr;
    char* user = nullptr;
    char* password = nullptr;
    char* port = nullptr;
    bool valid = set_result == CURLUE_OK
            && curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK
            && std::string(scheme) == "https"
            && curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK
            && curl_url_get(parsed, CURLUPART_USER, &user, 0) != CURLUE_OK
            && curl_url_get(parsed, CURLUPART_PASSWORD, &password, 0) != CURLUE_OK;
    if (valid && curl_url_get(parsed, CURLUPART_PORT, &port, 0) == CURLUE_OK) {
        valid = std::string(port) == "443";
    }
    if (valid) {
        std::string normalized_host(host);
        std::transform(normalized_host.begin(), normalized_host.end(),
                normalized_host.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
        constexpr char suffix[] = ".googlevideo.com";
        valid = normalized_host == "googlevideo.com"
                || (normalized_host.size() > sizeof(suffix) - 1
                    && normalized_host.compare(normalized_host.size() - sizeof(suffix) + 1,
                            sizeof(suffix) - 1, suffix) == 0);
    }
    curl_free(scheme);
    curl_free(host);
    curl_free(user);
    curl_free(password);
    curl_free(port);
    curl_url_cleanup(parsed);
    return valid;
}

}  // namespace

HttpUpstreamStream::HttpUpstreamStream(HttpUpstreamConfig config)
    : config_(config), buffer_(config.buffer_capacity) {
    if (config_.maximum_bytes == 0 || config_.connect_timeout_seconds <= 0
            || config_.low_speed_limit_bytes_per_second <= 0
            || config_.low_speed_time_seconds <= 0) {
        throw std::invalid_argument("invalid HTTP upstream limits");
    }
    if (!ensureCurlInitialized()) {
        throw std::runtime_error("libcurl global initialization failed");
    }
}

HttpUpstreamStream::~HttpUpstreamStream() {
    cancel();
    wait();
}

bool HttpUpstreamStream::isAllowedResolvedUrl(const std::string& value) {
    return validResolvedUrl(value);
}

bool HttpUpstreamStream::start(const std::string& resolved_https_url) {
    UpstreamState expected = UpstreamState::idle;
    if (!state_.compare_exchange_strong(expected, UpstreamState::running)) {
        return false;
    }
    if (!validResolvedUrl(resolved_https_url)) {
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            error_code_ = "invalid_upstream_url";
        }
        state_.store(UpstreamState::failed);
        buffer_.close();
        return false;
    }
    try {
        worker_ = std::thread(&HttpUpstreamStream::run, this, resolved_https_url);
    } catch (...) {
        state_.store(UpstreamState::failed);
        std::lock_guard<std::mutex> lock(error_mutex_);
        error_code_ = "worker_start_failed";
        buffer_.close();
        return false;
    }
    return true;
}

std::size_t HttpUpstreamStream::read(std::uint8_t* destination,
                                    const std::size_t maximum) {
    return buffer_.read(destination, maximum);
}

void HttpUpstreamStream::cancel() {
    cancelled_.store(true);
    buffer_.cancel();
}

void HttpUpstreamStream::wait() {
    if (worker_.joinable()) worker_.join();
}

UpstreamState HttpUpstreamStream::state() const noexcept {
    return state_.load();
}

std::string HttpUpstreamStream::error_code() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_code_;
}

std::uint64_t HttpUpstreamStream::bytes_received() const noexcept {
    return bytes_received_.load();
}

std::size_t HttpUpstreamStream::writeCallback(char* data, const std::size_t size,
                                               const std::size_t count, void* context) {
    auto* self = static_cast<HttpUpstreamStream*>(context);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const std::size_t length = size * count;
    if (self->cancelled_.load()) return 0;

    const std::uint64_t received = self->bytes_received_.load();
    if (length > self->config_.maximum_bytes
            || received > self->config_.maximum_bytes - length) {
        std::lock_guard<std::mutex> lock(self->error_mutex_);
        self->error_code_ = "stream_size_limit";
        return 0;
    }

    if (!self->buffer_.write(reinterpret_cast<const std::uint8_t*>(data), length)) {
        return 0;
    }
    self->bytes_received_.fetch_add(length);
    return length;
}

int HttpUpstreamStream::progressCallback(void* context, long, long, long, long) {
    const auto* self = static_cast<const HttpUpstreamStream*>(context);
    return self->cancelled_.load() ? 1 : 0;
}

void HttpUpstreamStream::run(std::string url) {
    if (config_.transport_override_for_tests) {
        const auto sink = [this](const std::uint8_t* data, const std::size_t length) {
            if (cancelled_.load()) return false;
            const std::uint64_t received = bytes_received_.load();
            if (length > config_.maximum_bytes
                    || received > config_.maximum_bytes - length) {
                std::lock_guard<std::mutex> lock(error_mutex_);
                error_code_ = "stream_size_limit";
                return false;
            }
            if (!buffer_.write(data, length)) return false;
            bytes_received_.fetch_add(length);
            return true;
        };
        bool success = false;
        try {
            success = config_.transport_override_for_tests(url, sink, &cancelled_);
        } catch (...) {
            success = false;
        }
        if (cancelled_.load()) {
            state_.store(UpstreamState::cancelled);
            buffer_.cancel();
        } else if (success) {
            state_.store(UpstreamState::completed);
            buffer_.close();
        } else {
            {
                std::lock_guard<std::mutex> lock(error_mutex_);
                if (error_code_.empty()) error_code_ = "upstream_transport_failed";
            }
            state_.store(UpstreamState::failed);
            buffer_.cancel();
        }
        return;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            error_code_ = "curl_init_failed";
        }
        state_.store(UpstreamState::failed);
        buffer_.close();
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    // Do not follow redirects to arbitrary HTTPS hosts. Resolve/refresh through yt-dlp instead.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.connect_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT,
                     config_.low_speed_limit_bytes_per_second);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, config_.low_speed_time_seconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &HttpUpstreamStream::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &HttpUpstreamStream::progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "yoinks-mpd/0.1");

    const CURLcode result = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    if (cancelled_.load()) {
        state_.store(UpstreamState::cancelled);
        buffer_.cancel();
        return;
    }

    if (result == CURLE_OK && response_code >= 200 && response_code < 300) {
        state_.store(UpstreamState::completed);
        buffer_.close();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        if (error_code_.empty()) {
            error_code_ = result == CURLE_OPERATION_TIMEDOUT
                    ? "upstream_timeout" : "upstream_http_error";
        }
    }
    state_.store(UpstreamState::failed);
    buffer_.cancel();
}

}  // namespace yoinks
