/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/http_server.hpp"
#include "yoinks/chunked_stream_writer.hpp"
#include "yoinks/lazy_stream_session_registry.hpp"
#include "yoinks/youtube_url.hpp"
#include "yoinks/mpd_playlist_watcher.hpp"
#include "yoinks/mpd_queue_manager.hpp"

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
#include <csignal>
#include <functional>
#include <cctype>
#include <memory>
#include <iostream>
#include <thread>
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
    return isAllowedYouTubeUrl(value);
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

http::status queueFailureStatus(const std::string& error_code) {
    if (error_code == "mpd_connect_failed"
            || error_code == "mpd_protocol_greeting_invalid"
            || error_code == "session_capacity_reached"
            || error_code == "mpd_reconcile_failed") {
        return http::status::service_unavailable;
    }
    if (error_code == "process_timeout") return http::status::gateway_timeout;
    if (error_code == "invalid_entry_count" || error_code == "no_supported_entries"
            || error_code == "playlist_limit_or_empty") {
        return http::status::unprocessable_entity;
    }
    return http::status::bad_gateway;
}

http::response<http::string_body> handle(
        const http::request<http::string_body>& request,
        const std::string& configured_token,
        const std::shared_ptr<YoutubeQueueService>& queue_service) {
    const auto target = request.target();
    if (request.method() == http::verb::get && target == "/api/v1/health") {
        return response(http::status::ok,
                R"({"status":"ok","service":"yoinks-mpd","api_version":1,"control_ready":true,"media_ready":false,"mpd_playback_verified":false})",
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
        if (!queue_service) {
            return response(http::status::service_unavailable,
                    R"({"success":false,"error":{"code":"backend_not_ready","message":"Playback backend is not configured"}})",
                    request.version());
        }
        const auto queued = queue_service->enqueue(youtube_url, mode == "play_now");
        Json result;
        result["success"] = queued.success;
        result["queued"] = queued.queued;
        result["started"] = queued.started;
        result["playlist"] = queued.queued > 1;
        result["rollback_complete"] = queued.rollback_complete;
        if (!queued.success) {
            result["error"] = {{"code", queued.error_code},
                               {"message", "YouTube queue request could not be completed"}};
        }
        const auto status = queued.success ? http::status::ok
                : queueFailureStatus(queued.error_code);
        return response(status, result.dump(), request.version());
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
                 const std::shared_ptr<LazyStreamSessionRegistry>& sessions,
                 const std::shared_ptr<YtDlpResolver>& resolver) {
    beast::error_code error;
    if (!sessions || !resolver || !sessions->contains(id)) {
        auto reply = response(http::status::not_found,
            R"({"success":false,"error":{"code":"stream_not_found","message":"Stream session not found"}})", 11);
        stream.expires_after(std::chrono::seconds(5));
        http::write(stream, reply, error);
        return;
    }
    if (!sessions->activate(id,
            [resolver](const std::string& video_id, std::string& source_url,
                       const std::atomic_bool* cancellation) {
                return resolver->resolveAudioUrl(video_id, source_url, cancellation);
            })) {
        const bool failed = sessions->isFailed(id);
        auto reply = response(failed ? http::status::bad_gateway : http::status::service_unavailable,
            failed
                ? R"({"success":false,"error":{"code":"stream_activation_failed","message":"Unable to activate media stream"}})"
                : R"({"success":false,"error":{"code":"stream_busy","message":"Stream session is already activating or capacity is full"}})",
            11);
        stream.expires_after(std::chrono::seconds(5));
        http::write(stream, reply, error);
        return;
    }
    const auto upstream = sessions->findActive(id);
    if (!upstream) {
        auto reply = response(http::status::bad_gateway,
            R"({"success":false,"error":{"code":"stream_unavailable","message":"Media stream is unavailable"}})", 11);
        stream.expires_after(std::chrono::seconds(5));
        http::write(stream, reply, error);
        sessions->release(id, true);
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
    if (error) { sessions->release(id, true); return; }

    writeChunkedStream(
        [&](std::uint8_t* destination, std::size_t maximum) {
            return upstream->read(destination, maximum);
        },
        [&] { return upstream->state() == UpstreamState::completed; },
        [&](const char* data, std::size_t size) {
            stream.expires_after(std::chrono::seconds(30));
            asio::write(stream, asio::buffer(data, size), error);
            return !error;
        });
    sessions->release(id, error || upstream->state() != UpstreamState::completed);
}

bool serveConnection(tcp::socket socket, const std::string& token,
                     const std::shared_ptr<LazyStreamSessionRegistry>& sessions,
                     const std::shared_ptr<YtDlpResolver>& resolver,
                     const std::shared_ptr<YoutubeQueueService>& queue_service,
                     const std::shared_ptr<std::atomic<std::size_t>>& active_streams,
                     asio::thread_pool& stream_workers,
                     const std::shared_ptr<std::atomic<std::size_t>>& active_connections) {
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
        return false;
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
            try {
                asio::post(stream_workers,
                    [stream = std::move(stream), id, sessions, resolver, active_streams,
                     active_connections]() mutable {
                        beast::error_code stream_error;
                        try { serveStream(stream, id, sessions, resolver); }
                        catch (...) { sessions->release(id, true); }
                        stream.socket().shutdown(tcp::socket::shutdown_both, stream_error);
                        stream.socket().close(stream_error);
                        active_streams->fetch_sub(1);
                        active_connections->fetch_sub(1);
                    });
                return true;
            } catch (...) {
                active_streams->fetch_sub(1);
                throw;
            }
        }
        stream.socket().shutdown(tcp::socket::shutdown_both, error);
        stream.socket().close(error);
        return false;
    }
    auto reply = handle(request, token, queue_service);
    stream.expires_after(std::chrono::seconds(5));
    http::write(stream, reply, error);
    stream.socket().shutdown(tcp::socket::shutdown_both, error);
    stream.socket().close(error);
    return false;
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
    asio::thread_pool stream_workers{2};
    auto active_connections = std::make_shared<std::atomic<std::size_t>>(0);
    auto active_streams = std::make_shared<std::atomic<std::size_t>>(0);
    auto sessions = config_.sessions;
    if (!sessions || !config_.resolver || !config_.queue_service) {
        throw std::invalid_argument("YouTube resolver, lazy sessions and queue service are required");
    }
    const auto address = asio::ip::make_address(config_.bind_address);
    if (!address.is_loopback()) {
        throw std::invalid_argument(
                "HTTP control API is loopback-only until TLS termination is implemented");
    }
    if (config_.playlist_watch_enabled && !config_.mpd_queue_manager) {
        throw std::invalid_argument("MPD queue manager is required when playlist watcher is enabled");
    }
    tcp::acceptor acceptor{context, tcp::endpoint{address, config_.port}};
    std::atomic_bool stop_watcher{false};
    asio::signal_set signals{context, SIGINT, SIGTERM};
    signals.async_wait([&](const beast::error_code&, int) {
        stop_watcher.store(true);
        beast::error_code ignored;
        acceptor.close(ignored);
    });

    std::thread playlist_watcher;
    if (config_.playlist_watch_enabled) {
        playlist_watcher = std::thread([&stop_watcher, watch_config = config_.playlist_watch,
                                        mpd = config_.mpd_queue_manager]() {
            MpdPlaylistWatcher watcher(watch_config);
            auto retry_delay = std::chrono::seconds(1);
            while (!stop_watcher.load()) {
                const auto event = watcher.waitForPlaylistChange();
                if (stop_watcher.load()) break;
                if (event.success) {
                    retry_delay = std::chrono::seconds(1);
                    if (event.playlist_changed) {
                        const auto snapshot = mpd->listPlaylistState();
                        if (snapshot.success) {
                            std::clog << "yoinks-mpd: playlist observed; entries="
                                      << snapshot.entries.size() << ", state="
                                      << snapshot.state << '\\n';
                        } else {
                            std::clog << "yoinks-mpd: playlist snapshot unavailable; code="
                                      << snapshot.error_code << '\\n';
                        }
                    }
                    continue;
                }
                if (!event.timed_out && !event.error_code.empty()) {
                    std::clog << "yoinks-mpd: playlist watcher reconnect; code="
                              << event.error_code << '\\n';
                    const auto until = std::chrono::steady_clock::now() + retry_delay;
                    while (!stop_watcher.load()
                            && std::chrono::steady_clock::now() < until) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    retry_delay = std::min(retry_delay * 2, std::chrono::seconds(10));
                }
            }
        });
    }

    auto accept_next = std::make_shared<std::function<void()>>();
    *accept_next = [&]() {
        if (!acceptor.is_open()) return;
        auto socket = std::make_shared<tcp::socket>(context);
        acceptor.async_accept(*socket,
            [&, socket, accept_next](const beast::error_code& accept_error) mutable {
                if (accept_error) {
                    if (accept_error != asio::error::operation_aborted) {
                        beast::error_code ignored;
                        acceptor.close(ignored);
                        signals.cancel(ignored);
                    }
                    return;
                }

                const std::size_t previous = active_connections->fetch_add(1);
                if (previous >= kMaximumActiveConnections) {
                    active_connections->fetch_sub(1);
                    rejectBusy(std::move(*socket));
                } else {
                    try {
                        asio::post(workers, [socket = std::move(*socket),
                                             token = config_.bearer_token,
                                             active_connections, active_streams, sessions,
                                             resolver = config_.resolver,
                                             queue_service = config_.queue_service,
                                             &stream_workers]() mutable {
                            bool handed_off = false;
                            try {
                                handed_off = serveConnection(std::move(socket), token,
                                    sessions, resolver, queue_service, active_streams,
                                    stream_workers, active_connections);
                            } catch (...) {
                                // A malformed client must not terminate a worker or expose details.
                            }
                            if (!handed_off) active_connections->fetch_sub(1);
                        });
                    } catch (...) {
                        active_connections->fetch_sub(1);
                    }
                }
                (*accept_next)();
            });
    };

    (*accept_next)();
    context.run();
    stop_watcher.store(true);
    if (playlist_watcher.joinable()) playlist_watcher.join();

    // Drain control requests first so no new stream jobs can be submitted.
    workers.join();
    // Cancel upstream producers and wake readers before joining stream workers.
    sessions->shutdown();
    stream_workers.join();
}

}  // namespace yoinks
