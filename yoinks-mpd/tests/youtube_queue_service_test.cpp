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
        }});
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", false);
    require(result.success && result.queued == 3, "successful playlist must be reported");
    require(received.size() == 3 && !play_now, "all proxy URIs must preserve order and mode");
    require(sessions->size() == 3 && sessions->activeCount() == 0,
            "queue creation must register lazy sessions without starting media");
    require(received[0].rfind("http://127.0.0.1:8788/stream/", 0) == 0,
            "MPD must receive only local proxy URIs");
}

void testKnownRollbackRemovesCapabilities() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    yoinks::YoutubeQueueService service({8788, 200, sessions,
        [](const std::string&) { return metadata(2); },
        [](const std::vector<std::string>&, bool) {
            yoinks::MpdQueueResult result; result.error_code = "mpd_add_rejected";
            result.rollback_complete = true; return result;
        }});
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
        }});
    const auto result = service.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && !result.rollback_complete && result.queued == 1,
            "ambiguous MPD result must not be reported as success");
    require(sessions->size() == 1, "ambiguous MPD result must retain referenced capability");
}

void testPlaylistLimitAndMetadataFailure() {
    auto sessions = std::make_shared<yoinks::LazyStreamSessionRegistry>();
    yoinks::YoutubeQueueService too_many({8788, 200, sessions,
        [](const std::string&) { return metadata(201); },
        [](const std::vector<std::string>&, bool) { return yoinks::MpdQueueResult{}; }});
    auto result = too_many.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && result.error_code == "invalid_entry_count",
            "playlist above 200 must be rejected");
    yoinks::YoutubeQueueService failed({8788, 200, sessions,
        [](const std::string&) { yoinks::MetadataResult value; value.error_code = "extractor_failed"; return value; },
        [](const std::vector<std::string>&, bool) { return yoinks::MpdQueueResult{}; }});
    result = failed.enqueue("https://youtu.be/abcdefghijk", false);
    require(!result.success && result.error_code == "extractor_failed" && sessions->size() == 0,
            "metadata failure must not create sessions");
}
}  // namespace

int main() {
    testSuccessfulQueueIsLazyAndOrdered();
    testKnownRollbackRemovesCapabilities();
    testUnknownOutcomeRetainsCapabilities();
    testPlaylistLimitAndMetadataFailure();
    std::cout << "YouTube queue service tests passed\n";
}
