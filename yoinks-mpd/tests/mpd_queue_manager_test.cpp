/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/mpd_queue_manager.hpp"

#include <boost/asio.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class MockMpd final {
public:
    explicit MockMpd(bool fail_second_add = false, bool fail_play = false,
                     bool drop_add_response = false, bool drop_play_response = false,
                     std::string snapshot_response =
                         "file: http://127.0.0.1:8788/stream/0123456789abcdef0123456789abcdef\n"
                         "Id: 700\nPos: 0\nTitle: Test track\nlist_OK\n"
                         "songid: 700\nstate: play\nOK\n")
        : acceptor_(context_, tcp::endpoint{asio::ip::address_v4::loopback(), 0}),
          fail_second_add_(fail_second_add), fail_play_(fail_play),
          drop_add_response_(drop_add_response), drop_play_response_(drop_play_response),
          snapshot_response_(std::move(snapshot_response)) {
        port_ = acceptor_.local_endpoint().port();
        worker_ = std::thread([this]() { serve(); });
    }

    ~MockMpd() {
        if (worker_.joinable()) {
            boost::system::error_code ignored;
            acceptor_.close(ignored);
            worker_.join();
        }
    }

    std::uint16_t port() const { return port_; }

    void join() {
        if (worker_.joinable()) worker_.join();
        if (worker_error_) std::rethrow_exception(worker_error_);
    }

    const std::vector<std::string>& commands() const { return commands_; }

private:
    void send(tcp::socket& socket, const std::string& value) {
        asio::write(socket, asio::buffer(value));
    }

    void serve() {
        try {
            tcp::socket socket(context_);
            acceptor_.accept(socket);
            send(socket, "OK MPD 0.23.15\n");
            asio::streambuf buffer;
            std::size_t add_count = 0;
            std::uint32_t next_id = 700;
            for (;;) {
                boost::system::error_code error;
                asio::read_until(socket, buffer, '\n', error);
                if (error) break;
                std::istream input(&buffer);
                std::string line;
                std::getline(input, line);
                commands_.push_back(line);
                if (line == "command_list_ok_begin") {
                    std::vector<std::string> batch;
                    for (;;) {
                        boost::system::error_code batch_error;
                        asio::read_until(socket, buffer, '\n', batch_error);
                        if (batch_error) return;
                        std::istream batch_input(&buffer);
                        std::string batch_line;
                        std::getline(batch_input, batch_line);
                        commands_.push_back(batch_line);
                        if (batch_line == "command_list_end") break;
                        batch.push_back(batch_line);
                    }
                    if (batch.size() != 2 || batch[0] != "playlistinfo" || batch[1] != "status") {
                        send(socket, "ACK [5@0] {command_list} invalid test batch\n");
                        continue;
                    }
                    send(socket, snapshot_response_);
                } else if (line.rfind("addid ", 0) == 0) {
                    ++add_count;
                    if (drop_add_response_ && add_count == 1) return;
                    if (fail_second_add_ && add_count == 2) {
                        send(socket, "ACK [50@0] {addid} simulated add failure\n");
                    } else {
                        send(socket, "Id: " + std::to_string(next_id++) + "\nOK\n");
                    }
                } else if (line.rfind("playid ", 0) == 0) {
                    if (drop_play_response_) return;
                    send(socket, fail_play_
                        ? "ACK [50@0] {playid} simulated play failure\n"
                        : "OK\n");
                } else if (line.rfind("deleteid ", 0) == 0) {
                    send(socket, "OK\n");
                } else if (line == "playlistinfo") {
                    send(socket,
                        "file: http://127.0.0.1:8788/stream/0123456789abcdef0123456789abcdef\n"
                        "Id: 700\nPos: 0\nTitle: Test track\nOK\n");
                } else if (line == "status") {
                    send(socket, "songid: 700\nstate: play\nOK\n");
                } else {
                    send(socket, "ACK [5@0] {unknown} unsupported test command\n");
                }
            }
        } catch (...) {
            worker_error_ = std::current_exception();
        }
    }

    asio::io_context context_;
    tcp::acceptor acceptor_;
    std::thread worker_;
    bool fail_second_add_;
    bool fail_play_;
    bool drop_add_response_;
    bool drop_play_response_;
    std::string snapshot_response_;
    std::uint16_t port_{0};
    std::vector<std::string> commands_;
    std::exception_ptr worker_error_;
};

std::vector<std::string> sampleUris() {
    return {
        "http://127.0.0.1:8788/stream/0123456789abcdef0123456789abcdef",
        "http://127.0.0.1:8788/stream/fedcba9876543210fedcba9876543210"
    };
}

yoinks::MpdQueueManager managerFor(const MockMpd& mock) {
    yoinks::MpdQueueManagerConfig config;
    config.port = mock.port();
    config.operation_timeout_milliseconds = 3000;
    return yoinks::MpdQueueManager(config);
}

void testEnqueueAppendsWithoutChangingPlayback() {
    MockMpd mock;
    auto manager = managerFor(mock);
    const auto result = manager.enqueue(sampleUris(), false);
    mock.join();
    require(result.success && result.queued == 2, "enqueue should append both proxy URIs");
    require(mock.commands().size() == 2, "enqueue should issue only addid commands");
    require(mock.commands()[0].rfind("addid ", 0) == 0
            && mock.commands()[1].rfind("addid ", 0) == 0,
            "queue append must use stable MPD song IDs");
}

void testPlayNowStartsFirstNewSongOnlyWhenRequested() {
    MockMpd mock;
    auto manager = managerFor(mock);
    const auto result = manager.enqueue(sampleUris(), true);
    mock.join();
    require(result.success && result.queued == 2, "play-now should add both entries");
    require(mock.commands().size() == 3, "play-now should issue one explicit playid");
    require(mock.commands()[0] == "addid \"" + sampleUris()[1] + "\" 0"
            && mock.commands()[1] == "addid \"" + sampleUris()[0] + "\" 0",
            "play-now must insert the playlist at the front in reverse insertion order");
    require(result.song_ids[0] == 701 && mock.commands()[2] == "playid 701",
            "play-now should start the first requested entry in final playlist order");
}

void testAddFailureRollsBackOnlyNewEntries() {
    MockMpd mock(true, false);
    auto manager = managerFor(mock);
    const auto result = manager.enqueue(sampleUris(), false);
    mock.join();
    require(!result.success && result.error_code == "mpd_add_rejected",
            "MPD add rejection should be reported");
    require(result.rollback_complete, "known added entries should be rolled back");
    require(mock.commands().size() == 3 && mock.commands()[2] == "deleteid 700",
            "rollback must delete only the entry added by this request");
    for (const auto& command : mock.commands()) {
        require(command != "clear" && command.rfind("single ", 0) != 0
                && command.rfind("random ", 0) != 0 && command.rfind("consume ", 0) != 0,
                "manager must not clear queue or change MPD playback modes");
    }
}

void testPlayNowRejectionRollsBackNewEntries() {
    MockMpd mock(false, true);
    auto manager = managerFor(mock);
    const auto result = manager.enqueue({sampleUris().front()}, true);
    mock.join();
    require(!result.success && result.error_code == "mpd_play_now_rejected",
            "playid rejection should be reported");
    require(result.rollback_complete, "rejected playid should allow rollback");
    require(mock.commands().size() == 3 && mock.commands()[2] == "deleteid 700",
            "failed play-now should remove only its newly-added item");
}


void testLostAddReplyIsReportedAsAmbiguousWithoutUnsafeRollback() {
    MockMpd mock(false, false, true, false);
    auto manager = managerFor(mock);
    const auto result = manager.enqueue({sampleUris().front()}, false);
    mock.join();
    require(!result.success && result.error_code == "mpd_add_outcome_unknown",
            "lost addid reply must be reported as an unknown outcome");
    require(!result.rollback_complete, "unknown add outcome cannot claim rollback completion");
    require(mock.commands().size() == 1, "unknown add outcome must not issue destructive deleteid");
}

void testLostPlayReplyDoesNotDeletePossiblyCurrentSong() {
    MockMpd mock(false, false, false, true);
    auto manager = managerFor(mock);
    const auto result = manager.enqueue({sampleUris().front()}, true);
    mock.join();
    require(!result.success && result.error_code == "mpd_play_now_outcome_unknown",
            "lost playid reply must be reported as an unknown outcome");
    require(!result.rollback_complete && result.queued == 1 && result.song_ids.size() == 1,
            "known added ID must be retained for reconciliation");
    require(mock.commands().size() == 2 && mock.commands()[1] == "playid 700",
            "ambiguous playid must not be followed by deleteid");
}
void testPlaylistSnapshotReturnsOnlyTrustedProxySessions() {
    MockMpd mock;
    auto manager = managerFor(mock);
    const auto snapshot = manager.listStreamSessionIds();
    mock.join();
    require(snapshot.success && snapshot.stream_session_ids.size() == 1,
            "playlist snapshot should find the active proxy session");
    require(snapshot.stream_session_ids.front() == "0123456789abcdef0123456789abcdef",
            "playlist snapshot should return only the opaque local session ID");
    require(mock.commands().size() == 1 && mock.commands()[0] == "playlistinfo",
            "session reconciliation must use a read-only MPD command");
}


void testFullPlaylistStateIncludesStableIdsPositionsAndPlayerStatus() {
    MockMpd mock;
    auto manager = managerFor(mock);
    const auto snapshot = manager.listPlaylistState();
    mock.join();
    require(snapshot.success, "full playlist snapshot should succeed");
    require(snapshot.entries.size() == 1, "snapshot should contain one playlist entry");
    require(snapshot.entries.front().song_id == 700
            && snapshot.entries.front().position == 0
            && snapshot.entries.front().uri == sampleUris().front(),
            "snapshot should preserve song ID, position and URI");
    require(snapshot.state == "play" && snapshot.current_song_id == 700,
            "snapshot should include current player state and song ID");
    require(mock.commands().size() == 4 && mock.commands()[0] == "command_list_ok_begin"
            && mock.commands()[1] == "playlistinfo" && mock.commands()[2] == "status"
            && mock.commands()[3] == "command_list_end",
            "snapshot must use one read-only MPD command list");
}

void expectSnapshotFailure(const std::string& response, const std::string& expected,
                           const std::string& message) {
    MockMpd mock(false, false, false, false, response);
    auto manager = managerFor(mock);
    const auto snapshot = manager.listPlaylistState();
    mock.join();
    require(!snapshot.success && snapshot.error_code == expected, message);
}

void testEmptyPlaylistStoppedIsValid() {
    MockMpd mock(false, false, false, false, "list_OK\nstate: stop\nOK\n");
    auto manager = managerFor(mock);
    const auto snapshot = manager.listPlaylistState();
    mock.join();
    require(snapshot.success && snapshot.entries.empty() && snapshot.state == "stop"
            && !snapshot.current_song_id.has_value(),
            "empty stopped playlist should be a valid snapshot");
}

void testPausedPlaylistRequiresAndPreservesCurrentSong() {
    const std::string response =
        "file: song.flac\nPos: 0\nId: 701\nlist_OK\n"
        "state: pause\nsongid: 701\nOK\n";
    MockMpd mock(false, false, false, false, response);
    auto manager = managerFor(mock);
    const auto snapshot = manager.listPlaylistState();
    mock.join();
    require(snapshot.success && snapshot.state == "pause"
            && snapshot.current_song_id == 701 && snapshot.entries.size() == 1,
            "paused snapshot should preserve its current song ID");
}

void testStoppedPlaylistMustNotContainCurrentSongId() {
    expectSnapshotFailure("list_OK\nstate: stop\nsongid: 700\nOK\n",
                          "mpd_status_invalid",
                          "stopped snapshot with songid must be rejected");
}

void testMalformedPlaylistEntriesAreRejected() {
    expectSnapshotFailure("file: song.flac\nPos: 0\nlist_OK\nstate: stop\nOK\n",
                          "mpd_playlist_entry_invalid", "entry without Id must be rejected");
    expectSnapshotFailure("file: song.flac\nId: 700\nlist_OK\nstate: stop\nOK\n",
                          "mpd_playlist_entry_invalid", "entry without Pos must be rejected");
    expectSnapshotFailure("Id: 700\nPos: 0\nlist_OK\nstate: stop\nOK\n",
                          "mpd_playlist_entry_invalid", "entry without file must be rejected");
}

void testInvalidAndUnknownPlayerStatesAreRejected() {
    expectSnapshotFailure("list_OK\nstate: buffering\nOK\n",
                          "mpd_status_invalid", "unknown MPD state must be rejected");
    expectSnapshotFailure("list_OK\nvolume: 50\nOK\n",
                          "mpd_status_invalid", "missing MPD state must be rejected");
}

void testCommandListAckAndMissingSeparatorAreRejected() {
    expectSnapshotFailure("ACK [5@1] {status} simulated failure\n",
                          "mpd_snapshot_rejected", "command-list ACK must fail closed");
    expectSnapshotFailure("state: stop\nOK\n",
                          "mpd_snapshot_protocol_invalid", "missing list_OK must be rejected");
}

void testSnapshotResponseLineLimitIsEnforced() {
    std::string response;
    for (std::size_t i = 0; i < 8320; ++i) response += "x\n";
    response += "list_OK\nstate: stop\nOK\n";
    expectSnapshotFailure(response, "mpd_snapshot_response_too_large",
                          "snapshot response line limit must be enforced");
}

void testSnapshotConnectionFailureIsReported() {
    asio::io_context context;
    tcp::acceptor acceptor(context, tcp::endpoint{asio::ip::address_v4::loopback(), 0});
    const auto unused_port = acceptor.local_endpoint().port();
    boost::system::error_code ignored;
    acceptor.close(ignored);
    yoinks::MpdQueueManagerConfig config;
    config.port = unused_port;
    config.operation_timeout_milliseconds = 500;
    yoinks::MpdQueueManager manager(config);
    const auto snapshot = manager.listPlaylistState();
    require(!snapshot.success && snapshot.error_code == "mpd_connect_failed",
            "closed MPD endpoint must report connection failure");
}

void testInvalidProxyUriIsRejectedBeforeConnecting() {
    yoinks::MpdQueueManagerConfig config;
    config.port = 1;
    yoinks::MpdQueueManager manager(config);
    const auto result = manager.enqueue({"https://cdn.example/audio.mp3"}, false);
    require(!result.success && result.error_code == "invalid_stream_uri",
            "external or non-proxy media URIs must be rejected");
    require(!manager.isAllowedStreamUri(
            "http://127.0.0.1:8788/stream/0123456789abcdef0123456789abcdef/extra"),
            "stream URI path must be exact");
}

}  // namespace

int main() {
    try {
        testEnqueueAppendsWithoutChangingPlayback();
        testPlayNowStartsFirstNewSongOnlyWhenRequested();
        testAddFailureRollsBackOnlyNewEntries();
        testPlayNowRejectionRollsBackNewEntries();
        testLostAddReplyIsReportedAsAmbiguousWithoutUnsafeRollback();
        testLostPlayReplyDoesNotDeletePossiblyCurrentSong();
        testPlaylistSnapshotReturnsOnlyTrustedProxySessions();
        testFullPlaylistStateIncludesStableIdsPositionsAndPlayerStatus();
        testEmptyPlaylistStoppedIsValid();
        testPausedPlaylistRequiresAndPreservesCurrentSong();
        testStoppedPlaylistMustNotContainCurrentSongId();
        testMalformedPlaylistEntriesAreRejected();
        testInvalidAndUnknownPlayerStatesAreRejected();
        testCommandListAckAndMissingSeparatorAreRejected();
        testSnapshotResponseLineLimitIsEnforced();
        testSnapshotConnectionFailureIsReported();
        testInvalidProxyUriIsRejectedBeforeConnecting();
        std::cout << "All MPD queue manager tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}