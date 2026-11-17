/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/mpd_playlist_watcher.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace yoinks {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

bool readLine(beast::tcp_stream& stream, asio::streambuf& buffer,
              std::string& line,
              std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    stream.expires_after(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
    boost::system::error_code error;
    asio::read_until(stream, buffer, '\n', error);
    if (error) return false;
    std::istream input(&buffer);
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

bool writeLine(beast::tcp_stream& stream, const std::string& line,
               std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    stream.expires_after(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
    boost::system::error_code error;
    asio::write(stream, asio::buffer(line), error);
    return !error;
}

}  // namespace

MpdPlaylistWatcher::MpdPlaylistWatcher(MpdPlaylistWatchConfig config)
    : config_(std::move(config)) {
    boost::system::error_code error;
    const auto address = asio::ip::make_address(config_.host, error);
    if (error || !address.is_loopback() || config_.port == 0
            || config_.operation_timeout_milliseconds == 0) {
        throw std::invalid_argument("invalid loopback MPD playlist watcher configuration");
    }
}

MpdPlaylistWatchResult MpdPlaylistWatcher::waitForPlaylistChange() const {
    MpdPlaylistWatchResult result;
    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(config_.operation_timeout_milliseconds);
    asio::io_context context;
    beast::tcp_stream stream{context};
    stream.expires_after(std::chrono::milliseconds(config_.operation_timeout_milliseconds));
    boost::system::error_code error;
    stream.connect(tcp::endpoint{asio::ip::make_address(config_.host), config_.port}, error);
    if (error) {
        result.error_code = "mpd_connect_failed";
        return result;
    }

    asio::streambuf buffer{8192};
    std::string line;
    if (!readLine(stream, buffer, line, deadline) || line.rfind("OK MPD ", 0) != 0) {
        result.error_code = "mpd_protocol_greeting_invalid";
        return result;
    }
    if (!writeLine(stream, "idle playlist\n", deadline)) {
        result.error_code = "mpd_idle_write_failed";
        return result;
    }

    std::size_t bytes = 0;
    bool changed = false;
    for (std::size_t count = 0; count < 32; ++count) {
        if (!readLine(stream, buffer, line, deadline)) {
            // A deadline expiry is a normal wake-up opportunity for the owner;
            // other transport failures are reported distinctly.
            result.timed_out = std::chrono::steady_clock::now() >= deadline;
            result.error_code = result.timed_out ? "" : "mpd_idle_read_failed";
            return result;
        }
        bytes += line.size() + 1;
        if (bytes > 8192) {
            result.error_code = "mpd_idle_response_too_large";
            return result;
        }
        if (line == "changed: playlist") changed = true;
        if (line == "OK") {
            result.success = true;
            result.playlist_changed = changed;
            return result;
        }
        if (line.rfind("ACK ", 0) == 0) {
            result.error_code = "mpd_idle_rejected";
            return result;
        }
    }
    result.error_code = "mpd_idle_response_invalid";
    return result;
}

}  // namespace yoinks
