/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/lazy_stream_session_registry.hpp"
#include <array>
#include <cerrno>
#include <stdexcept>
#include <sys/random.h>
#include <utility>

namespace yoinks {

LazyStreamSessionRegistry::LazyStreamSessionRegistry(LazyStreamSessionRegistryConfig config)
    : config_(std::move(config)) {
    constexpr std::size_t maximum_ram_buffer_bytes = 8 * 1024 * 1024;
    if (config_.maximum_sessions == 0 || config_.maximum_sessions > 200
            || config_.maximum_active_streams == 0 || config_.maximum_active_streams > 2
            || config_.upstream.buffer_capacity == 0
            || config_.upstream.buffer_capacity > maximum_ram_buffer_bytes / config_.maximum_active_streams) {
        throw std::invalid_argument("invalid lazy stream session limits");
    }
}

LazyStreamSessionRegistry::~LazyStreamSessionRegistry() { clear(); }

bool LazyStreamSessionRegistry::validVideoId(const std::string& id) noexcept {
    if (id.size() != 11) return false;
    for (const unsigned char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

std::string LazyStreamSessionRegistry::generateId() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) { if (errno == EINTR) continue; throw std::runtime_error("secure session ID generation failed"); }
        if (count == 0) throw std::runtime_error("secure session ID generation returned EOF");
        offset += static_cast<std::size_t>(count);
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string id; id.reserve(32);
    for (const unsigned char byte : bytes) { id.push_back(hex[byte >> 4]); id.push_back(hex[byte & 15]); }
    return id;
}

std::string LazyStreamSessionRegistry::create(const std::string& video_id) {
    if (!validVideoId(video_id)) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessions_.size() >= config_.maximum_sessions) return {};
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        const auto id = generateId();
        if (sessions_.emplace(id, Entry{video_id, nullptr, false, false, false, false, std::make_shared<std::atomic_bool>(false)}).second) return id;
    }
    return {};
}

bool LazyStreamSessionRegistry::activate(const std::string& id, const ResolveAudioUrl& resolver) {
    std::string video_id;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::shared_ptr<HttpUpstreamStream> previous;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end() || found->second.activating || found->second.serving) return false;

        if (found->second.stream && !found->second.refresh_required
                && found->second.stream->state() == UpstreamState::running) {
            found->second.serving = true;
            return true;
        }

        std::size_t active = 0;
        for (const auto& item : sessions_) {
            if (item.first == id) continue;
            if (item.second.activating || item.second.serving
                    || (item.second.stream
                        && item.second.stream->state() == UpstreamState::running)) {
                ++active;
            }
        }
        if (active >= config_.maximum_active_streams) return false;

        previous = std::move(found->second.stream);
        found->second.refresh_required = false;
        found->second.activating = true;
        found->second.failed = false;
        video_id = found->second.video_id;
        cancellation = found->second.cancellation;
    }
    if (previous) previous->wait();

    std::string source_url;
    bool resolved = false;
    try { resolved = resolver && resolver(video_id, source_url, cancellation.get()); } catch (...) { resolved = false; }
    if (cancellation->load()) resolved = false;
    if (!resolved || !HttpUpstreamStream::isAllowedResolvedUrl(source_url)) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found != sessions_.end()) {
            found->second.activating = false;
            found->second.failed = true;
        }
        return false;
    }

    auto stream = std::make_shared<HttpUpstreamStream>(config_.upstream);
    if (!stream->start(source_url)) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found != sessions_.end()) {
            found->second.activating = false;
            found->second.failed = true;
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end() || cancellation->load()) {
            stream->cancel();
            stream->wait();
            return false;
        }
        found->second.stream = std::move(stream);
        found->second.activating = false;
        found->second.failed = false;
        found->second.serving = true;
    }
    return true;
}

std::shared_ptr<HttpUpstreamStream> LazyStreamSessionRegistry::findActive(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(id);
    return found == sessions_.end() ? nullptr : found->second.stream;
}

bool LazyStreamSessionRegistry::erase(const std::string& id) {
    std::shared_ptr<HttpUpstreamStream> stream;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end()) return false;
        if (found->second.cancellation) found->second.cancellation->store(true);
        stream = std::move(found->second.stream);
        sessions_.erase(found);
    }
    if (stream) { stream->cancel(); stream->wait(); }
    return true;
}

void LazyStreamSessionRegistry::clear() {
    std::unordered_map<std::string, Entry> removed;
    { 
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : sessions_) if (item.second.cancellation) item.second.cancellation->store(true);
        removed.swap(sessions_);
    }
    for (auto& item : removed) if (item.second.stream) item.second.stream->cancel();
    for (auto& item : removed) if (item.second.stream) item.second.stream->wait();
}

bool LazyStreamSessionRegistry::contains(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(id) != sessions_.end();
}

bool LazyStreamSessionRegistry::isFailed(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(id);
    return found != sessions_.end() && found->second.failed;
}

bool LazyStreamSessionRegistry::release(const std::string& id, bool cancel_upstream) {
    std::shared_ptr<HttpUpstreamStream> stream;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end()) return false;
        found->second.serving = false;
        if (cancel_upstream) {
            found->second.refresh_required = true;
            found->second.activating = true;  // reserve the slot until cancellation is joined
            stream = std::move(found->second.stream);
        } else if (found->second.stream
                && found->second.stream->state() != UpstreamState::running) {
            // Keep only the lightweight video-ID capability after EOF/failure.
            // The fixed-size RAM audio buffer is released immediately.
            stream = std::move(found->second.stream);
        }
    }
    if (stream) {
        if (cancel_upstream) stream->cancel();
        stream->wait();
    }
    if (cancel_upstream) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sessions_.find(id);
        if (found != sessions_.end()) found->second.activating = false;
    }
    return true;
}

std::size_t LazyStreamSessionRegistry::retainOnly(
        const std::unordered_set<std::string>& live_ids) {
    std::unordered_map<std::string, Entry> removed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            const bool active = it->second.activating || it->second.serving
                    || (it->second.stream
                        && it->second.stream->state() == UpstreamState::running);
            if (live_ids.find(it->first) != live_ids.end() || active) {
                ++it;
                continue;
            }
            if (it->second.cancellation) it->second.cancellation->store(true);
            auto current = it++;
            removed.insert(std::move(*current));
            sessions_.erase(current);
        }
    }
    for (auto& item : removed) if (item.second.stream) item.second.stream->cancel();
    for (auto& item : removed) if (item.second.stream) item.second.stream->wait();
    return removed.size();
}

std::size_t LazyStreamSessionRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_); return sessions_.size();
}

std::size_t LazyStreamSessionRegistry::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& item : sessions_) {
        if (item.second.activating || item.second.serving || (item.second.stream
                && item.second.stream->state() == UpstreamState::running)) ++count;
    }
    return count;
}

}  // namespace yoinks