/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/mpd_queue_manager.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace yoinks {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

bool validOpaqueId(const std::string& id) {
    if (id.size() != 32) return false;
    for (const unsigned char c : id) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool readLine(beast::tcp_stream& stream, asio::streambuf& buffer,
              std::string& line, std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    stream.expires_after(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
    beast::error_code error;
    asio::read_until(stream, buffer, '\n', error);
    if (error) return false;
    std::istream input(&buffer);
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

bool writeCommand(beast::tcp_stream& stream, const std::string& command,
                  std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    stream.expires_after(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
    beast::error_code error;
    asio::write(stream, asio::buffer(command), error);
    return !error;
}

struct CommandReply {
    bool success{false};
    bool rejected{false};
    std::vector<std::string> lines;
};

CommandReply command(beast::tcp_stream& stream, asio::streambuf& buffer,
                     const std::string& text,
                     std::chrono::steady_clock::time_point deadline) {
    CommandReply reply;
    if (text.find('\0') != std::string::npos || text.find_first_of("\r\n") != std::string::npos
            || !writeCommand(stream, text + "\n", deadline)) return reply;
    std::size_t bytes = 0;
    for (std::size_t count = 0; count < 32; ++count) {
        std::string line;
        if (!readLine(stream, buffer, line, deadline)) return reply;
        bytes += line.size() + 1;
        if (bytes > 8192) return reply;
        if (line == "OK") { reply.success = true; return reply; }
        if (line.rfind("ACK ", 0) == 0) { reply.rejected = true; return reply; }
        reply.lines.push_back(std::move(line));
    }
    return reply;
}

bool parseSongId(const CommandReply& reply, std::uint32_t& id) {
    for (const auto& line : reply.lines) {
        constexpr char prefix[] = "Id: ";
        if (line.rfind(prefix, 0) != 0) continue;
        const std::string value = line.substr(sizeof(prefix) - 1);
        std::uint32_t parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
            id = parsed;
            return true;
        }
    }
    return false;
}

bool rollback(beast::tcp_stream& stream, asio::streambuf& buffer,
              const std::vector<std::uint32_t>& ids,
              std::chrono::steady_clock::time_point deadline) {
    bool complete = true;
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        if (!command(stream, buffer, "deleteid " + std::to_string(*it), deadline).success) {
            complete = false;
        }
    }
    return complete;
}

}  // namespace

MpdQueueManager::MpdQueueManager(MpdQueueManagerConfig config)
    : config_(std::move(config)) {
    boost::system::error_code error;
    const auto address = asio::ip::make_address(config_.host, error);
    if (error || !address.is_loopback() || config_.port == 0 || config_.stream_port == 0
            || config_.operation_timeout_milliseconds == 0
            || config_.maximum_entries == 0 || config_.maximum_entries > 200) {
        throw std::invalid_argument("invalid loopback MPD queue manager configuration");
    }
}

bool MpdQueueManager::isAllowedStreamUri(const std::string& uri) const noexcept {
    const std::string prefix = "http://127.0.0.1:" + std::to_string(config_.stream_port)
            + "/stream/";
    if (uri.size() != prefix.size() + 32 || uri.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    return validOpaqueId(uri.substr(prefix.size()));
}

MpdQueueResult MpdQueueManager::enqueue(const std::vector<std::string>& stream_uris,
                                        bool play_now) const {
    MpdQueueResult result;
    if (stream_uris.empty() || stream_uris.size() > config_.maximum_entries) {
        result.error_code = "invalid_entry_count";
        return result;
    }
    for (const auto& uri : stream_uris) {
        if (!isAllowedStreamUri(uri)) {
            result.error_code = "invalid_stream_uri";
            return result;
        }
    }

    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(config_.operation_timeout_milliseconds);
    asio::io_context context;
    beast::tcp_stream stream{context};
    stream.expires_after(std::chrono::milliseconds(config_.operation_timeout_milliseconds));
    beast::error_code error;
    stream.connect(tcp::endpoint{asio::ip::make_address(config_.host), config_.port}, error);
    if (error) { result.error_code = "mpd_connect_failed"; return result; }
    asio::streambuf buffer{8192};
    std::string greeting;
    if (!readLine(stream, buffer, greeting, deadline) || greeting.rfind("OK MPD ", 0) != 0) {
        result.error_code = "mpd_protocol_greeting_invalid";
        return result;
    }

    for (const auto& uri : stream_uris) {
        const auto reply = command(stream, buffer, "addid \"" + uri + "\"", deadline);
        std::uint32_t id = 0;
        if (!reply.success) {
            result.rollback_complete = rollback(stream, buffer, result.song_ids, deadline);
            if (!reply.rejected) result.rollback_complete = false;
            result.error_code = reply.rejected ? "mpd_add_rejected" : "mpd_add_outcome_unknown";
            result.queued = 0;
            result.song_ids.clear();
            return result;
        }
        if (!parseSongId(reply, id)) {
            result.rollback_complete = rollback(stream, buffer, result.song_ids, deadline);
            result.rollback_complete = false;
            result.error_code = "mpd_add_response_invalid";
            result.queued = 0;
            result.song_ids.clear();
            return result;
        }
        result.song_ids.push_back(id);
    }

    if (play_now) {
        const auto reply = command(stream, buffer, "playid "
                + std::to_string(result.song_ids.front()), deadline);
        if (!reply.success) {
            if (reply.rejected) {
                result.rollback_complete = rollback(stream, buffer, result.song_ids, deadline);
            } else {
                // The command may have started playback even if its reply was lost.
                // Do not delete a possibly-current song on an ambiguous outcome.
                result.rollback_complete = false;
            }
            result.error_code = reply.rejected ? "mpd_play_now_rejected"
                    : "mpd_play_now_outcome_unknown";
            result.queued = 0;
            result.song_ids.clear();
            return result;
        }
    }

    result.success = true;
    result.queued = result.song_ids.size();
    return result;
}

}  // namespace yoinks