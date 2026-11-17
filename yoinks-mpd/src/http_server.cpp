/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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
    const std::size_t count = std::max(left.size(), right.size());
    unsigned int difference = static_cast<unsigned int>(left.size() ^ right.size());
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char a = i < left.size()
                ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size()
                ? static_cast<unsigned char>(right[i]) : 0;
        difference |= static_cast<unsigned int>(a ^ b);
    }
    return difference == 0;
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

void serveConnection(tcp::socket socket, const std::string& token) {
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
    auto reply = handle(request, token);
    stream.expires_after(std::chrono::seconds(5));
    http::write(stream, reply, error);
    stream.socket().shutdown(tcp::socket::shutdown_both, error);
    stream.socket().close(error);
}

}  // namespace

HttpServer::HttpServer(HttpServerConfig config) : config_(std::move(config)) {
    if (config_.bearer_token.size() < 32) {
        throw std::invalid_argument("YOINKS_MPD_TOKEN must contain at least 32 characters");
    }
}

void HttpServer::run() {
    asio::io_context context{1};
    const auto address = asio::ip::make_address(config_.bind_address);
    tcp::acceptor acceptor{context, tcp::endpoint{address, config_.port}};
    for (;;) {
        tcp::socket socket{context};
        acceptor.accept(socket);
        serveConnection(std::move(socket), config_.bearer_token);
    }
}

}  // namespace yoinks
