/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "yoinks/http_upstream_stream.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yoinks {

struct StreamSessionRegistryConfig {
    std::size_t maximum_sessions{4};
    HttpUpstreamConfig upstream{};
};

/**
 * Owns a bounded set of active upstream sessions.
 *
 * Session IDs are 128-bit kernel-random capabilities encoded as lowercase
 * hexadecimal. The registry never exposes source URLs. Callers must keep IDs
 * private and serve them only over the loopback HTTP listener.
 */
class StreamSessionRegistry final {
public:
    explicit StreamSessionRegistry(StreamSessionRegistryConfig config = {});

    StreamSessionRegistry(const StreamSessionRegistry&) = delete;
    StreamSessionRegistry& operator=(const StreamSessionRegistry&) = delete;

    // Returns an opaque ID when upstream startup succeeds, otherwise empty.
    // The maximum session count bounds aggregate buffer allocation.
    std::string create(const std::string& trusted_resolved_url);

    std::shared_ptr<HttpUpstreamStream> find(const std::string& id) const;

    // Removes the capability and cancels the associated transfer.
    bool erase(const std::string& id);

    // Cancels and removes every session.
    void clear();

    std::size_t size() const;

private:
    static std::string generateId();

    StreamSessionRegistryConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<HttpUpstreamStream>> sessions_;
};

}  // namespace yoinks
