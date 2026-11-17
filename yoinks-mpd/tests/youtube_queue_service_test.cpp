/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/youtube_queue_service.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

yoinks::MetadataResult metadata(std::size_t count) {
    yoinks::MetadataResult result;
    result.success = true;
    for (std::size_t i = 0; i < count; ++i) {
        yoinks::ResolvedMediaEntry entry;
        entry.video_id = "abcdefghijk";
        result.entries.push_back(entry);
    }
    return result;
}

void testSuccessfulQueueIsLazyAndOrdered() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    std::vector<std::string> received;
    bool play_now = true;
    yoinks::YoutubeQueueService service({8788, 200, sessions,
        [](const std::string&) { return metadata(3); },
        [&](const std::vector<std::string>& uris, bool requested_play_now) {
            received = uris; play_now = requested_play_now;
            yoinks::MpdQueueResult result; result.success = true; result.queued = uris.size();
            return result;
        }, {}});
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", false);
    require(result.success && result.queued == 3, "successful playlist must be reported");
    require(received.size() == 3 && !play_now, "all proxy URIs must preserve order and mode");
    require(sessions->size() == 3 && sessions->activeCount() == 0,
            "queue creation must register lazy sessions without starting media");
    require(received[0].rfind("http://127.0.0.1:8788/stream/", 0) == 0,
            "MPD must receive only local proxy URIs");
}

void testPlayNowIsReportedOnlyAfterQueueConfirmation() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    yoinks::YoutubeQueueService service({8788, 200, sessions,
        [](const std::string&) { return metadata(1); },
        [](const std::vector<std::string>&, bool play_now) {
            yoinks::MpdQueueResult result;
            result.success = play_now;
            result.queued = play_now ? 1 : 0;
            return result;
        }, {}});
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", true);
    require(result.success && result.started && result.queued == 1,
            "play-now must be reported only after MPD confirms the operation");
}

void testKnownRollbackRemovesCapabilities() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    yoinks::YoutubeQueueService service({8788, 200, sessions,
        [](const std::string&) { return metadata(2); },
        [](const std::vector<std::string>&, bool) {
            yoinks::MpdQueueResult result; result.error_code = "mpd_add_rejected";
            result.rollback_complete = true; return result;
        }, {}});
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && result.rollback_complete, "known rollback must fail closed");
    require(sessions->size() == 0, "rolled-back entries must be removed");
}

void testUnknownOutcomeRetainsCapabilities() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    yoinks::YoutubeQueueService service({8788, 200, sessions,
        [](const std::string&) { return metadata(1); },
        [](const std::vector<std::string>&, bool) {
            yoinks::MpdQueueResult result; result.error_code = "mpd_add_outcome_unknown";
            result.rollback_complete = false; result.queued = 1; return result;
        }, {}});
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && !result.rollback_complete && result.queued == 1,
            "ambiguous MPD result must not be reported as success");
    require(sessions->size() == 1, "ambiguous MPD result must retain referenced capability");
}

void testMpdSnapshotPrunesStaleSessionsBeforeQueueing() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    const auto stale = sessions->create("dQw4w9WgXcQ");
    yoinks::YoutubeQueueServiceConfig config;
    config.stream_port = 8788;
    config.sessions = sessions;
    config.resolve_metadata = [](const std::string&) { return metadata(1); };
    config.enqueue = [](const std::vector<std::string>& uris, bool) {
        yoinks::MpdQueueResult result; result.success = true; result.queued = uris.size(); return result;
    };
    config.queued_session_ids = [](std::vector<std::string>& ids) {
        ids.clear();
        return true;
    };
    yoinks::YoutubeQueueService service(std::move(config));
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", false);
    require(result.success && !sessions->contains(stale) && sessions->size() == 1,
            "MPD reconciliation must remove stale capabilities before allocating new ones");
}

void testMpdSnapshotFailurePreventsQueueMutation() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    bool enqueue_called = false;
    yoinks::YoutubeQueueServiceConfig config;
    config.stream_port = 8788;
    config.sessions = sessions;
    config.resolve_metadata = [](const std::string&) { return metadata(1); };
    config.enqueue = [&](const std::vector<std::string>&, bool) {
        enqueue_called = true; return yoinks::MpdQueueResult{};
    };
    config.queued_session_ids = [](std::vector<std::string>&) { return false; };
    yoinks::YoutubeQueueService service(std::move(config));
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && result.error_code == "mpd_reconcile_failed",
            "MPD snapshot failure must be surfaced");
    require(!enqueue_called && sessions->size() == 0,
            "queue mutation and session allocation must stop when reconciliation fails");
}

void testPlaylistLimitAndMetadataFailure() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    yoinks::YoutubeQueueService too_many({8788, 200, sessions,
        [](const std::string&) { return metadata(201); },
        [](const std::vector<std::string>&, bool) { return yoinks::MpdQueueResult{}; }, {}});
    auto result = too_many.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && result.error_code == "invalid_entry_count",
            "playlist above 200 must be rejected");
    yoinks::YoutubeQueueService failed({8788, 200, sessions,
        [](const std::string&) { yoinks::MetadataResult value; value.error_code = "extractor_failed"; return value; },
        [](const std::vector<std::string>&, bool) { return yoinks::MpdQueueResult{}; }, {}});
    result = failed.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && result.error_code == "extractor_failed" && sessions->size() == 0,
            "metadata failure must not create sessions");
}
}  // namespace

int main() {
    testSuccessfulQueueIsLazyAndOrdered();
    testPlayNowIsReportedOnlyAfterQueueConfirmation();
    testKnownRollbackRemovesCapabilities();
    testUnknownOutcomeRetainsCapabilities();
    testMpdSnapshotPrunesStaleSessionsBeforeQueueing();
    testMpdSnapshotFailurePreventsQueueMutation();
    testPlaylistLimitAndMetadataFailure();
    std::cout << "YouTube queue service tests passed\n";
}
