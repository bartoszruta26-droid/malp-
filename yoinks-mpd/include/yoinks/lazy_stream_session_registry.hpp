/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "yoinks/http_upstream_stream.hpp"
#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace yoinks {

using ResolveAudioUrl = std::function<bool(const std::string&, std::string&, const std::atomic_bool*)>;

struct LazyStreamSessionRegistryConfig {
    std::size_t maximum_sessions{200};
    std::size_t maximum_active_streams{2};
    HttpUpstreamConfig upstream{};
    // Deterministic lifecycle-race seam; unset in production.
    std::function<void()> after_upstream_start_for_tests;
};

/**
 * Stores playlist capabilities without resolving CDN URLs or starting media transfers.
 * A source URL is resolved only when activate() is called for an HTTP stream GET.
 */
class LazyStreamSessionRegistry final {
public:
    explicit LazyStreamSessionRegistry(LazyStreamSessionRegistryConfig config = {});
    ~LazyStreamSessionRegistry();
    LazyStreamSessionRegistry(const LazyStreamSessionRegistry&) = delete;
    LazyStreamSessionRegistry& operator=(const LazyStreamSessionRegistry&) = delete;

    // Registers only a validated YouTube video ID. No network or media buffer is started.
    std::string create(const std::string& video_id);
    // Resolves and starts exactly one upstream stream on first GET.
    bool activate(const std::string& id, const ResolveAudioUrl& resolver);
    std::shared_ptr<HttpUpstreamStream> findActive(const std::string& id) const;
    bool contains(const std::string& id) const;
    bool isFailed(const std::string& id) const;
    bool release(const std::string& id, bool cancel_upstream);
    std::size_t retainOnly(const std::unordered_set<std::string>& live_ids);
    bool erase(const std::string& id);
    void clear();
    // Stops new create/activate operations, cancels sessions, and waits for all
    // in-flight activate() calls to leave before the owner destroys this object.
    // The owner must stop dispatching other public calls before invoking shutdown().
    void shutdown();
    std::size_t size() const;
    std::size_t activeCount() const;

private:
    struct Entry {
        std::string video_id;
        std::shared_ptr<HttpUpstreamStream> stream;
        bool activating{false};
        bool failed{false};
        bool serving{false};
        bool refresh_required{false};
        std::shared_ptr<std::atomic_bool> cancellation;
    };
    static std::string generateId();
    static bool validVideoId(const std::string& video_id) noexcept;
    struct ActivationOperationGuard {
        explicit ActivationOperationGuard(LazyStreamSessionRegistry* owner) noexcept;
        ~ActivationOperationGuard();
        LazyStreamSessionRegistry* owner;
    };
    void finishActivationOperation() noexcept;
    LazyStreamSessionRegistryConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable shutdown_condition_;
    std::size_t active_activations_{0};
    bool shutting_down_{false};
    bool shutdown_complete_{false};
    std::unordered_map<std::string, Entry> sessions_;
};

}  // namespace yoinks