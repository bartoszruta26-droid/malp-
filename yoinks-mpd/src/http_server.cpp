/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_server.hpp"
#include "yoinks/stream_session_registry.hpp"

#include <boost/asio.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <sstream>
#include <chrono>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace yoinks {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using Json = nlohmann::json;

constexpr std::size_t kMaxBodyBytes = 16 * 1024;
constexpr std::size_t kMaxHeaderBytes = 8 * 1024;

bool constantTimeEqual(const std::string& left, const std::string& right) {
    constexpr std::size_t kComparedBytes = 128;
    unsigned int difference = static_cast<unsigned int>(left.size() ^ right.size());
    for (std::size_t i = 0; i < kComparedBytes; ++i) {
        const unsigned char a = i < left.size()
                ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size()
                ? static_cast<unsigned char>(right[i]) : 0;
        difference |= static_cast<unsigned int>(a ^ b);
    }
    return difference == 0;
}

bool validIdentifier(const std::string& value, std::size_t minimum, std::size_t maximum) {
    if (value.size() < minimum || value.size() > maximum) return false;
    for (const unsigned char character : value) {
        if (!((character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '_' || character == '-')) return false;
    }
    return true;
}

bool allowedYouTubeUrl(const std::string& value) {
    constexpr char scheme[] = "https://";
    if (value.compare(0, sizeof(scheme) - 1, scheme) != 0
            || value.find('#') != std::string::npos
            || value.find('@') != std::string::npos) return false;
    const std::size_t authority_start = sizeof(scheme) - 1;
    const std::size_t path_start = value.find('/', authority_start);
    if (path_start == std::string::npos) return false;
    const std::string host = value.substr(authority_start, path_start - authority_start);
    if (host != "youtube.com" && host != "www.youtube.com"
            && host != "m.youtube.com" && host != "music.youtube.com"
            && host != "youtu.be") return false;

    const std::string path_and_query = value.substr(path_start);
    const std::size_t query_start = path_and_query.find('?');
    const std::string path = path_and_query.substr(0, query_start);
    const std::string query = query_start == std::string::npos
            ? "" : path_and_query.substr(query_start + 1);

    if (host == "youtu.be") {
        return path.size() == 12 && validIdentifier(path.substr(1), 11, 11);
    }
    if (path == "/watch") {
        const std::string key = "v=";
        const std::size_t position = query.find(key);
        if (position == std::string::npos
                || (position != 0 && query[position - 1] != '&')) return false;
        const std::size_t start = position + key.size();
        const std::size_t end = query.find('&', start);
        return validIdentifier(query.substr(start, end == std::string::npos
                ? std::string::npos : end - start), 11, 11);
    }
    if (path == "/playlist") {
        const std::string key = "list=";
        const std::size_t position = query.find(key);
        if (position == std::string::npos
                || (position != 0 && query[position - 1] != '&')) return false;
        const std::size_t start = position + key.size();
        const std::size_t end = query.find('&', start);
        return validIdentifier(query.substr(start, end == std::string::npos
                ? std::string::npos : end - start), 10, 128);
    }
    for (const std::string prefix : {"/shorts/", "/live/", "/embed/"}) {
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return path.size() == prefix.size() + 11
                    && validIdentifier(path.substr(prefix.size()), 11, 11);
        }
    }
    return false;
}

std::string bearerValue(const http::request<http::string_body>& request) {
    const auto value = request[http::field::authorization];
    constexpr char prefix[] = "Bearer ";
    const std::string header(value.data(), value.size());
    if (header.compare(0, sizeof(prefix) - 1, prefix) != 0) return {};
    return header.substr(sizeof(prefix) - 1);
}

http::response<http::string_body> response(
        http::status status, const std::string& body,
        unsigned version, bool keep_alive = false) {
    http::response<http::string_body> result{status, version};
    result.set(http::field::server, "yoinks-mpd");
    result.set(http::field::content_type, "application/json; charset=utf-8");
    result.set(http::field::cache_control, "no-store");
    result.keep_alive(keep_alive);
    result.body() = body;
    result.prepare_payload();
    return result;
}

http::response<http::string_body> handle(
        const http::request<http::string_body>& request,
        const std::string& configured_token) {
    const auto target = request.target();
    if (request.method() == http::verb::get && target == "/api/v1/health") {
        return response(http::status::ok,
                R"({"status":"ok","service":"yoinks-mpd","api_version":1,"control_ready":false})",
                request.version(), request.keep_alive());
    }

    if (target == "/api/v1/queue") {
        if (request.method() != http::verb::post) {
            return response(http::status::method_not_allowed,
                    R"({"success":false,"error":{"code":"method_not_allowed","message":"Method not allowed"}})",
                    request.version());
        }
        if (!constantTimeEqual(bearerValue(request), configured_token)) {
            return response(http::status::unauthorized,
                    R"({"success":false,"error":{"code":"unauthorized","message":"Authentication required"}})",
                    request.version());
        }

        Json body;
        try {
            body = Json::parse(request.body());
        } catch (const Json::exception&) {
            return response(http::status::bad_request,
                    R"({"success":false,"error":{"code":"invalid_json","message":"Invalid JSON body"}})",
                    request.version());
        }
        if (!body.is_object() || !body.contains("url") || !body["url"].is_string()
                || !body.contains("mode") || !body["mode"].is_string()) {
            return response(http::status::bad_request,
                    R"({"success":false,"error":{"code":"invalid_request","message":"url and mode are required"}})",
                    request.version());
        }
        const std::string youtube_url = body["url"].get<std::string>();
        if (!allowedYouTubeUrl(youtube_url)) {
            return response(http::status::unprocessable_entity,
                    R"({"success":false,"error":{"code":"invalid_url","message":"Unsupported URL"}})",
                    request.version());
        }
        const std::string mode = body["mode"].get<std::string>();
        if (mode != "enqueue" && mode != "play_now") {
            return response(http::status::bad_request,
                    R"({"success":false,"error":{"code":"invalid_mode","message":"Unsupported playback mode"}})",
                    request.version());
        }
        // Deliberately fail closed until resolver, HTTP relay and MPD queue manager
        // are implemented. Never claim that an item was queued when it was not.
        return response(http::status::service_unavailable,
                R"({"success":false,"error":{"code":"backend_not_ready","message":"Playback backend is not ready"}})",
                request.version());
    }

    return response(http::status::not_found,
            R"({"success":false,"error":{"code":"not_found","message":"Endpoint not found"}})",
            request.version());
}

void rejectBusy(tcp::socket socket) {
    beast::tcp_stream stream{std::move(socket)};
    stream.expires_after(std::chrono::seconds(1));
    auto reply = response(http::status::service_unavailable,
            R"({"success":false,"error":{"code":"server_busy","message":"Server capacity reached"}})",
            11);
    beast::error_code error;
    http::write(stream, reply, error);
    stream.socket().shutdown(tcp::socket::shutdown_both, error);
    stream.socket().close(error);
}


bool validStreamId(const std::string& id) {
    if (id.size() != 32) return false;
    for (const unsigned char c : id) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

void serveStream(beast::tcp_stream& stream, const std::string& id,
                 const std::shared_ptr<StreamSessionRegistry>& sessions) {
    const auto upstream = sessions->find(id);
    beast::error_code error;
    if (!upstream) {
        auto reply = response(http::status::not_found,
            R"({"success":false,"error":{"code":"stream_not_found","message":"Stream session not found"}})", 11);
        stream.expires_after(std::chrono::seconds(5));
        http::write(stream, reply, error);
        return;
    }
    http::response<http::empty_body> header{http::status::ok, 11};
    header.set(http::field::server, "yoinks-mpd");
    header.set(http::field::content_type, "application/octet-stream");
    header.set(http::field::cache_control, "no-store");
    header.set(http::field::accept_ranges, "none");
    header.chunked(true);
    header.keep_alive(false);
    http::response_serializer<http::empty_body> serializer{header};
    stream.expires_after(std::chrono::seconds(10));
    http::write_header(stream, serializer, error);
    if (error) { sessions->erase(id); return; }

    std::array<std::uint8_t, 16384> block{};
    bool completed = false;
    while (!error) {
        const std::size_t count = upstream->read(block.data(), block.size());
        if (count == 0) {
            completed = upstream->state() == UpstreamState::completed;
            break;
        }
        std::ostringstream line;
        line << std::hex << count << "\r\n";
        const std::string chunk_header = line.str();
        stream.expires_after(std::chrono::seconds(30));
        asio::write(stream, asio::buffer(chunk_header), error);
        if (!error) asio::write(stream, asio::buffer(block.data(), count), error);
        if (!error) asio::write(stream, asio::buffer("\r\n", 2), error);
    }
    if (completed && !error) {
        stream.expires_after(std::chrono::seconds(5));
        asio::write(stream, asio::buffer("0\r\n\r\n", 5), error);
    }
    sessions->erase(id);
}

void serveConnection(tcp::socket socket, const std::string& token,
                     const std::shared_ptr<StreamSessionRegistry>& sessions,
                     const std::shared_ptr<std::atomic<std::size_t>>& active_streams) {
    beast::tcp_stream stream{std::move(socket)};
    stream.expires_after(std::chrono::seconds(10));
    beast::error_code error;
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(kMaxBodyBytes);
    parser.header_limit(kMaxHeaderBytes);

    http::read(stream, buffer, parser, error);
    if (error) {
        if (error == http::error::body_limit) {
            auto reply = response(http::status::payload_too_large,
                    R"({"success":false,"error":{"code":"request_too_large","message":"Request body too large"}})",
                    11);
            stream.expires_after(std::chrono::seconds(5));
            http::write(stream, reply, error);
        }
        stream.socket().shutdown(tcp::socket::shutdown_both, error);
        stream.socket().close(error);
        return;
    }

    auto request = parser.release();
    const std::string target(request.target().data(), request.target().size());
    if (target.compare(0, 8, "/stream/") == 0) {
        const std::string id = target.substr(8);
        auto send_error = [&](http::status status, const std::string& body) {
            auto reply = response(status, body, request.version());
            stream.expires_after(std::chrono::seconds(5));
            http::write(stream, reply, error);
        };
        if (request.method() != http::verb::get) {
            send_error(http::status::method_not_allowed,
                R"({"success":false,"error":{"code":"method_not_allowed","message":"GET required"}})");
        } else if (request.find(http::field::range) != request.end()) {
            auto reply = response(http::status::not_implemented,
                R"({"success":false,"error":{"code":"range_not_supported","message":"Range requests are not supported"}})",
                request.version());
            reply.set(http::field::accept_ranges, "none");
            stream.expires_after(std::chrono::seconds(5));
            http::write(stream, reply, error);
        } else if (!validStreamId(id)) {
            send_error(http::status::not_found,
                R"({"success":false,"error":{"code":"stream_not_found","message":"Stream session not found"}})");
        } else if (active_streams->fetch_add(1) >= 2) {
            active_streams->fetch_sub(1);
            send_error(http::status::service_unavailable,
                R"({"success":false,"error":{"code":"stream_capacity_reached","message":"Stream capacity reached"}})");
        } else {
            try { serveStream(stream, id, sessions); } catch (...) { sessions->erase(id); }
            active_streams->fetch_sub(1);
        }
        stream.socket().shutdown(tcp::socket::shutdown_both, error);
        stream.socket().close(error);
        return;
    }
    auto reply = handle(request, token);
    stream.expires_after(std::chrono::seconds(5));
    http::write(stream, reply, error);
    stream.socket().shutdown(tcp::socket::shutdown_both, error);
    stream.socket().close(error);
}

}  // namespace

HttpServer::HttpServer(HttpServerConfig config) : config_(std::move(config)) {
    if (config_.bearer_token.size() < 43 || config_.bearer_token.size() > 128
            || !validIdentifier(config_.bearer_token, 43, 128)) {
        throw std::invalid_argument(
                "YOINKS_MPD_TOKEN must be 43-128 base64url characters (at least 256 bits)");
    }
}

void HttpServer::run() {
    constexpr std::size_t kWorkerCount = 4;
    constexpr std::size_t kMaximumActiveConnections = 16;
    asio::io_context context{1};
    asio::thread_pool workers{kWorkerCount};
    auto active_connections = std::make_shared<std::atomic<std::size_t>>(0);
    auto active_streams = std::make_shared<std::atomic<std::size_t>>(0);
    auto sessions = std::make_shared<StreamSessionRegistry>();
    const auto address = asio::ip::make_address(config_.bind_address);
    if (!address.is_loopback()) {
        throw std::invalid_argument(
                "HTTP control API is loopback-only until TLS termination is implemented");
    }
    tcp::acceptor acceptor{context, tcp::endpoint{address, config_.port}};
    for (;;) {
        tcp::socket socket{context};
        acceptor.accept(socket);
        const std::size_t previous = active_connections->fetch_add(1);
        if (previous >= kMaximumActiveConnections) {
            active_connections->fetch_sub(1);
            rejectBusy(std::move(socket));
            continue;
        }
        asio::post(workers, [socket = std::move(socket),
                             token = config_.bearer_token,
                             active_connections, active_streams, sessions]() mutable {
            try {
                serveConnection(std::move(socket), token, sessions, active_streams);
            } catch (...) {
                // A malformed client must not terminate a worker or expose details.
            }
            active_connections->fetch_sub(1);
        });
    }
}

}  // namespace yoinks
