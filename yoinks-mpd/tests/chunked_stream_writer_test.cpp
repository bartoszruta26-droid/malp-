/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/chunked_stream_writer.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testProgressiveChunkFramingAndSuccessfulEof() {
    const std::string payload(20000, 'x');
    std::size_t offset = 0;
    std::string wire;
    const bool completed = yoinks::writeChunkedStream(
        [&](std::uint8_t* destination, std::size_t maximum) {
            const std::size_t count = std::min(maximum, payload.size() - offset);
            std::copy_n(reinterpret_cast<const std::uint8_t*>(payload.data() + offset),
                        count, destination);
            offset += count;
            return count;
        },
        [] { return true; },
        [&](const char* data, std::size_t size) {
            wire.append(data, size);
            return true;
        });
    require(completed, "successful upstream must complete chunk writer");
    require(offset == payload.size(), "reader must be consumed progressively");
    require(wire.substr(0, 4) == "4000" && wire.substr(4, 2) == "\r\n",
            "first chunk must be capped at 16 KiB");
    require(wire.size() > payload.size(), "wire output must include chunk framing");
    require(wire.compare(wire.size() - 5, 5, "0\r\n\r\n") == 0,
            "successful upstream must emit exactly the terminal chunk");
}

void testFailedUpstreamDoesNotEmitSuccessfulEof() {
    const std::string payload = "partial-audio";
    std::size_t offset = 0;
    std::string wire;
    const bool completed = yoinks::writeChunkedStream(
        [&](std::uint8_t* destination, std::size_t maximum) {
            const std::size_t count = std::min(maximum, payload.size() - offset);
            std::copy_n(reinterpret_cast<const std::uint8_t*>(payload.data() + offset),
                        count, destination);
            offset += count;
            return count;
        },
        [] { return false; },
        [&](const char* data, std::size_t size) {
            wire.append(data, size);
            return true;
        });
    require(!completed, "failed upstream must not be reported as complete");
    require(wire.find("0\r\n\r\n") == std::string::npos,
            "failed upstream must not emit a terminal chunk");
}

void testSocketWriteFailureStopsImmediately() {
    const std::string payload = "audio";
    std::size_t reads = 0;
    std::size_t writes = 0;
    const bool completed = yoinks::writeChunkedStream(
        [&](std::uint8_t* destination, std::size_t) {
            ++reads;
            if (reads > 1) return std::size_t{0};
            std::copy(payload.begin(), payload.end(), destination);
            return payload.size();
        },
        [] { return true; },
        [&](const char*, std::size_t) {
            ++writes;
            return false;
        });
    require(!completed, "write failure must abort the stream");
    require(reads == 1 && writes == 1,
            "writer must stop reading immediately after socket write failure");
}

}  // namespace

int main() {
    try {
        testProgressiveChunkFramingAndSuccessfulEof();
        testFailedUpstreamDoesNotEmitSuccessfulEof();
        testSocketWriteFailureStopsImmediately();
        std::cout << "All chunked stream writer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
