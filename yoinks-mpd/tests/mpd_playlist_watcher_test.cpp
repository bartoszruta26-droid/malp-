/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/mpd_playlist_watcher.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class IdleFixture final {
public:
    explicit IdleFixture(std::string response, std::chrono::milliseconds delay = std::chrono::milliseconds{0},
                         bool send_response = true, std::string greeting = "OK MPD 0.23.15\\n",\n                         bool expect_command = true)
        : acceptor_(context_, tcp::endpoint{asio::ip::address_v4::loopback(), 0}),
          response_(std::move(response)), delay_(delay), send_response_(send_response),
          greeting_(std::move(greeting)), expect_command_(expect_command),\n          port_(acceptor_.local_endpoint().port()),
          worker_([this]() { serve(); }) {}

    ~IdleFixture() {
        if (worker_.joinable()) {
            boost::system::error_code ignored;
            acceptor_.close(ignored);
            worker_.join();
        }
    }

    std::uint16_t port() const { return port_; }
    void join() {
        if (worker_.joinable()) worker_.join();
        if (error_) std::rethrow_exception(error_);
    }
    const std::string& command() const { return command_; }

private:
    void serve() {
        try {
            tcp::socket socket(context_);
            acceptor_.accept(socket);
            asio::write(socket, asio::buffer(std::string{"OK MPD 0.23.15\n"}));
            asio::streambuf buffer;
            asio::read_until(socket, buffer, '\n');
            std::istream input(&buffer);
            std::getline(input, command_);
            if (delay_.count() > 0) std::this_thread::sleep_for(delay_);
            if (send_response_) {
                boost::system::error_code write_error;
                asio::write(socket, asio::buffer(response_), write_error);
            }
        } catch (...) {
            error_ = std::current_exception();
        }
    }

    asio::io_context context_;
    tcp::acceptor acceptor_;
    std::string response_;
    std::chrono::milliseconds delay_;
    bool send_response_;
    std::string greeting_;
    bool expect_command_;
    std::uint16_t port_;
    std::thread worker_;
    std::string command_;
    std::exception_ptr error_;
};

void testIdlePlaylistNotification() {
    IdleFixture fixture("changed: playlist\nOK\n");
    yoinks::MpdPlaylistWatchConfig config;
    config.port = fixture.port();
    config.operation_timeout_milliseconds = 3000;
    yoinks::MpdPlaylistWatcher watcher(config);
    const auto result = watcher.waitForPlaylistChange();
    fixture.join();
    require(result.success && result.playlist_changed && !result.timed_out,
            "idle playlist event must be surfaced to the caller");
    require(fixture.command() == "idle playlist",
            "watcher must subscribe only to playlist events");
}

void testIdleWithoutPlaylistChange() {
    IdleFixture fixture("changed: player\nOK\n");
    yoinks::MpdPlaylistWatchConfig config;
    config.port = fixture.port();
    config.operation_timeout_milliseconds = 3000;
    yoinks::MpdPlaylistWatcher watcher(config);
    const auto result = watcher.waitForPlaylistChange();
    fixture.join();
    require(result.success && !result.playlist_changed,
            "unrelated MPD subsystem events must not trigger playlist processing");
}

void testIdleSubscriptionTimesOut() {
    IdleFixture fixture("", std::chrono::milliseconds{250}, false);
    yoinks::MpdPlaylistWatchConfig config;
    config.port = fixture.port();
    config.operation_timeout_milliseconds = 100;
    yoinks::MpdPlaylistWatcher watcher(config);
    const auto result = watcher.waitForPlaylistChange();
    fixture.join();
    require(!result.success && result.timed_out && result.error_code.empty(),
            "idle deadline must be reported as a normal timeout, not a transport failure");
}

void testRejectsInvalidGreeting() {
    IdleFixture fixture("", std::chrono::milliseconds{0}, false, "NOT MPD\\n", false);
    yoinks::MpdPlaylistWatchConfig config;
    config.port = fixture.port();
    config.operation_timeout_milliseconds = 1000;
    yoinks::MpdPlaylistWatcher watcher(config);
    const auto result = watcher.waitForPlaylistChange();
    fixture.join();
    require(!result.success && result.error_code == "mpd_protocol_greeting_invalid",
            "watcher must reject a non-MPD protocol greeting");
}

void testRejectsIdleAck() {
    IdleFixture fixture("ACK [5@0] {} unknown command\\n");
    yoinks::MpdPlaylistWatchConfig config;
    config.port = fixture.port();
    config.operation_timeout_milliseconds = 1000;
    yoinks::MpdPlaylistWatcher watcher(config);
    const auto result = watcher.waitForPlaylistChange();
    fixture.join();
    require(!result.success && result.error_code == "mpd_idle_rejected",
            "MPD ACK must be returned as a protocol rejection");
}

void testConnectFailureIsReported() {
    asio::io_context context;
    tcp::acceptor reservation(context, tcp::endpoint{asio::ip::address_v4::loopback(), 0});
    const auto port = reservation.local_endpoint().port();
    boost::system::error_code close_error;
    reservation.close(close_error);
    require(!close_error, "test must release its reserved loopback port");

    yoinks::MpdPlaylistWatchConfig config;
    config.port = port;
    config.operation_timeout_milliseconds = 1000;
    yoinks::MpdPlaylistWatcher watcher(config);
    const auto result = watcher.waitForPlaylistChange();
    require(!result.success && result.error_code == "mpd_connect_failed",
            "connection refusal must be reported with a stable non-sensitive error code");
}

void testRejectsNonLoopbackHost() {
    yoinks::MpdPlaylistWatchConfig config;
    config.host = "192.0.2.10";
    bool rejected = false;
    try { yoinks::MpdPlaylistWatcher watcher(config); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "watcher must reject non-loopback MPD endpoints");
}
}  // namespace

int main() {
    try {
        testIdlePlaylistNotification();
        testIdleWithoutPlaylistChange();
        testIdleSubscriptionTimesOut();
        testRejectsInvalidGreeting();
        testRejectsIdleAck();
        testConnectFailureIsReported();
        testRejectsNonLoopbackHost();
        std::cout << "All MPD playlist watcher tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
