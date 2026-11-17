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

bool parseUnsigned(const std::string& value, std::uint32_t& parsed) {
    if (value.empty()) return false;
    std::uint32_t candidate = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), candidate);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return false;
    parsed = candidate;
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
                     std::chrono::steady_clock::time_point deadline,
                     std::size_t maximum_lines = 32,
                     std::size_t maximum_bytes = 8192) {
    CommandReply reply;
    if (text.find('\0') != std::string::npos || text.find_first_of("\r\n") != std::string::npos
            || !writeCommand(stream, text + "\n", deadline)) return reply;
    std::size_t bytes = 0;
    for (std::size_t count = 0; count < maximum_lines; ++count) {
        std::string line;
        if (!readLine(stream, buffer, line, deadline)) return reply;
        bytes += line.size() + 1;
        if (bytes > maximum_bytes) return reply;
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
        return parseUnsigned(line.substr(sizeof(prefix) - 1), id);
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

bool MpdQueueManager::isAllowedStreamUri(const std::string& uri) const {
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

    std::vector<std::uint32_t> added_song_ids;
    std::vector<std::uint32_t> song_ids_by_input(stream_uris.size(), 0);
    added_song_ids.reserve(stream_uris.size());

    for (std::size_t step = 0; step < stream_uris.size(); ++step) {
        const std::size_t index = play_now ? stream_uris.size() - 1 - step : step;
        std::string add_command = "addid \"" + stream_uris[index] + "\"";
        if (play_now) add_command += " 0";
        const auto reply = command(stream, buffer, add_command, deadline);
        std::uint32_t id = 0;
        if (!reply.success) {
            result.rollback_complete = rollback(stream, buffer, added_song_ids, deadline);
            if (!reply.rejected) result.rollback_complete = false;
            result.error_code = reply.rejected ? "mpd_add_rejected" : "mpd_add_outcome_unknown";
            if (result.rollback_complete) {
                result.queued = 0;
                result.song_ids.clear();
            } else {
                result.song_ids = added_song_ids;
                result.queued = result.song_ids.size();
            }
            return result;
        }
        if (!parseSongId(reply, id)) {
            result.rollback_complete = rollback(stream, buffer, added_song_ids, deadline);
            result.rollback_complete = false;
            result.error_code = "mpd_add_response_invalid";
            result.song_ids = added_song_ids;
            result.queued = result.song_ids.size();
            return result;
        }
        added_song_ids.push_back(id);
        song_ids_by_input[index] = id;
    }
    result.song_ids = song_ids_by_input;

    if (play_now) {
        const auto reply = command(stream, buffer, "playid "
                + std::to_string(result.song_ids.front()), deadline);
        if (!reply.success) {
            if (reply.rejected) {
                result.rollback_complete = rollback(stream, buffer, result.song_ids, deadline);
            } else {
                result.rollback_complete = false;
            }
            result.error_code = reply.rejected ? "mpd_play_now_rejected"
                    : "mpd_play_now_outcome_unknown";
            if (result.rollback_complete) {
                result.queued = 0;
                result.song_ids.clear();
            } else {
                result.queued = result.song_ids.size();
            }
            return result;
        }
    }

    result.success = true;
    result.queued = result.song_ids.size();
    return result;
}

MpdQueueSnapshot MpdQueueManager::listStreamSessionIds() const {
    MpdQueueSnapshot result;
    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(config_.operation_timeout_milliseconds);
    asio::io_context context;
    beast::tcp_stream stream{context};
    stream.expires_after(std::chrono::milliseconds(config_.operation_timeout_milliseconds));
    beast::error_code error;
    stream.connect(tcp::endpoint{asio::ip::make_address(config_.host), config_.port}, error);
    if (error) {
        result.error_code = "mpd_connect_failed";
        return result;
    }
    asio::streambuf buffer{1024 * 1024};
    std::string greeting;
    if (!readLine(stream, buffer, greeting, deadline) || greeting.rfind("OK MPD ", 0) != 0) {
        result.error_code = "mpd_protocol_greeting_invalid";
        return result;
    }
    const auto reply = command(stream, buffer, "playlistinfo", deadline, 8192, 1024 * 1024);
    if (!reply.success) {
        result.error_code = reply.rejected ? "mpd_playlistinfo_rejected" : "mpd_playlistinfo_failed";
        return result;
    }
    const std::string prefix = "http://127.0.0.1:" + std::to_string(config_.stream_port) + "/stream/";
    for (const auto& line : reply.lines) {
        constexpr char file_prefix[] = "file: ";
        if (line.rfind(file_prefix, 0) != 0) continue;
        const std::string uri = line.substr(sizeof(file_prefix) - 1);
        if (isAllowedStreamUri(uri)) result.stream_session_ids.push_back(uri.substr(prefix.size()));
    }
    result.success = true;
    return result;
}

MpdPlaylistStateSnapshot MpdQueueManager::listPlaylistState() const {
    MpdPlaylistStateSnapshot result;
    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(config_.operation_timeout_milliseconds);
    asio::io_context context;
    beast::tcp_stream stream{context};
    stream.expires_after(std::chrono::milliseconds(config_.operation_timeout_milliseconds));
    beast::error_code error;
    stream.connect(tcp::endpoint{asio::ip::make_address(config_.host), config_.port}, error);
    if (error) { result.error_code = "mpd_connect_failed"; return result; }

    asio::streambuf buffer{1024 * 1024};
    std::string greeting;
    if (!readLine(stream, buffer, greeting, deadline) || greeting.rfind("OK MPD ", 0) != 0) {
        result.error_code = "mpd_protocol_greeting_invalid";
        return result;
    }

    // MPD command lists keep both read operations in one protocol transaction,
    // preventing a client-side gap between playlistinfo and status.
    const std::string batch = "command_list_ok_begin\nplaylistinfo\nstatus\ncommand_list_end\n";
    if (!writeCommand(stream, batch, deadline)) {
        result.error_code = "mpd_snapshot_write_failed";
        return result;
    }
    CommandReply playlist;
    CommandReply status;
    bool status_section = false;
    bool saw_separator = false;
    std::size_t response_bytes = 0;
    for (std::size_t count = 0; count < 8320; ++count) {
        std::string line;
        if (!readLine(stream, buffer, line, deadline)) {
            result.error_code = "mpd_snapshot_read_failed";
            return result;
        }
        response_bytes += line.size() + 1;
        if (response_bytes > 1024 * 1024) {
            result.error_code = "mpd_snapshot_response_too_large";
            return result;
        }
        if (line == "ACK " || line.rfind("ACK ", 0) == 0) {
            result.error_code = "mpd_snapshot_rejected";
            return result;
        }
        if (line == "list_OK") {
            if (saw_separator || status_section) {
                result.error_code = "mpd_snapshot_protocol_invalid";
                return result;
            }
            saw_separator = true;
            status_section = true;
            continue;
        }
        if (line == "OK") {
            if (!saw_separator || !status_section) {
                result.error_code = "mpd_snapshot_protocol_invalid";
                return result;
            }
            playlist.success = true;
            status.success = true;
            break;
        }
        (status_section ? status.lines : playlist.lines).push_back(std::move(line));
        if (count == 8319) {
            result.error_code = "mpd_snapshot_response_too_large";
            return result;
        }
    }
    if (!playlist.success || !status.success) {
        result.error_code = "mpd_snapshot_protocol_invalid";
        return result;
    }

    MpdPlaylistEntry entry;
    bool have_entry = false;
    bool have_id = false;
    bool have_position = false;
    const auto finish_entry = [&]() -> bool {
        if (!have_entry) return true;
        if (!have_id || !have_position || entry.uri.empty()) return false;
        result.entries.push_back(entry);
        entry = MpdPlaylistEntry{};
        have_entry = false;
        have_id = false;
        have_position = false;
        return true;
    };

    for (const auto& line : playlist.lines) {
        constexpr char file_prefix[] = "file: ";
        constexpr char id_prefix[] = "Id: ";
        constexpr char pos_prefix[] = "Pos: ";
        if (line.rfind(file_prefix, 0) == 0) {
            if (!finish_entry()) { result.error_code = "mpd_playlist_entry_invalid"; return result; }
            entry.uri = line.substr(sizeof(file_prefix) - 1);
            have_entry = true;
        } else if (line.rfind(id_prefix, 0) == 0 && have_entry) {
            have_id = parseUnsigned(line.substr(sizeof(id_prefix) - 1), entry.song_id);
            if (!have_id) { result.error_code = "mpd_playlist_entry_invalid"; return result; }
        } else if (line.rfind(pos_prefix, 0) == 0 && have_entry) {
            std::uint32_t position = 0;
            have_position = parseUnsigned(line.substr(sizeof(pos_prefix) - 1), position);
            if (!have_position) { result.error_code = "mpd_playlist_entry_invalid"; return result; }
            entry.position = static_cast<std::size_t>(position);
        }
    }
    if (!finish_entry()) { result.error_code = "mpd_playlist_entry_invalid"; return result; }

    bool have_state = false;
    for (const auto& line : status.lines) {
        constexpr char state_prefix[] = "state: ";
        constexpr char song_id_prefix[] = "songid: ";
        if (line.rfind(state_prefix, 0) == 0) {
            result.state = line.substr(sizeof(state_prefix) - 1);
            have_state = result.state == "play" || result.state == "pause" || result.state == "stop";
            if (!have_state) { result.error_code = "mpd_status_invalid"; return result; }
        } else if (line.rfind(song_id_prefix, 0) == 0) {
            std::uint32_t song_id = 0;
            if (!parseUnsigned(line.substr(sizeof(song_id_prefix) - 1), song_id)) {
                result.error_code = "mpd_status_invalid";
                return result;
            }
            result.current_song_id = song_id;
        }
    }
    if (!have_state || (result.state == "stop" && result.current_song_id.has_value())
            || (result.state != "stop" && !result.current_song_id.has_value())) {
        result.error_code = "mpd_status_invalid";
        return result;
    }
    if (result.current_song_id.has_value()) {
        const auto found = std::find_if(result.entries.begin(), result.entries.end(),
            [&result](const MpdPlaylistEntry& item) { return item.song_id == *result.current_song_id; });
        if (found == result.entries.end()) { result.error_code = "mpd_status_song_missing"; return result; }
    }
    result.success = true;
    return result;
}

}  // namespace yoinks
