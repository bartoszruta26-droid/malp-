/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/stream_session_registry.hpp"

#include <array>
#include <cerrno>
#include <stdexcept>
#include <sys/random.h>

namespace yoinks {

StreamSessionRegistry::StreamSessionRegistry(StreamSessionRegistryConfig config)
    : config_(config) {
    constexpr std::size_t kMaximumAggregateBufferBytes = 8 * 1024 * 1024;
    if (config_.maximum_sessions == 0 || config_.maximum_sessions > 32) {
        throw std::invalid_argument("maximum stream sessions must be between 1 and 32");
    }
    if (config_.upstream.buffer_capacity == 0
            || config_.upstream.buffer_capacity
                    > kMaximumAggregateBufferBytes / config_.maximum_sessions) {
        throw std::invalid_argument("aggregate stream RAM buffers must not exceed 8 MiB");
    }
}

StreamSessionRegistry::~StreamSessionRegistry() {
    clear();
}

std::string StreamSessionRegistry::generateId() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("secure session ID generation failed");
        }
        if (count == 0) throw std::runtime_error("secure session ID generation returned EOF");
        offset += static_cast<std::size_t>(count);
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::string StreamSessionRegistry::create(const std::string& trusted_resolved_url) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sessions_.size() >= config_.maximum_sessions) return {};
    }

    auto stream = std::make_shared<HttpUpstreamStream>(config_.upstream);
    if (!stream->start(trusted_resolved_url)) return {};

    std::lock_guard<std::mutex> lock(mutex_);
    if (sessions_.size() >= config_.maximum_sessions) {
        stream->cancel();
        stream->wait();
        return {};
    }

    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        const std::string id = generateId();
        const auto inserted = sessions_.emplace(id, stream);
        if (inserted.second) return id;
    }

    stream->cancel();
    stream->wait();
    return {};
}

std::shared_ptr<HttpUpstreamStream> StreamSessionRegistry::find(
        const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(id);
    return found == sessions_.end() ? nullptr : found->second;
}

bool StreamSessionRegistry::erase(const std::string& id) {
    std::shared_ptr<HttpUpstreamStream> stream;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end()) return false;
        stream = std::move(found->second);
        sessions_.erase(found);
    }
    stream->cancel();
    stream->wait();
    return true;
}

void StreamSessionRegistry::clear() {
    std::unordered_map<std::string, std::shared_ptr<HttpUpstreamStream>> removed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        removed.swap(sessions_);
    }
    for (auto& entry : removed) {
        entry.second->cancel();
    }
    for (auto& entry : removed) {
        entry.second->wait();
    }
}

std::size_t StreamSessionRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

}  // namespace yoinks
