/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/bounded_stream_buffer.hpp"

#include <array>
#include <condition_variable>
#include <mutex>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testRejectsZeroCapacity() {
    bool threw = false;
    try { yoinks::BoundedStreamBuffer buffer(0); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "zero capacity must be rejected");
}

void testWraparoundAndBoundedReads() {
    yoinks::BoundedStreamBuffer buffer(5);
    const std::array<std::uint8_t, 4> first{{1, 2, 3, 4}};
    require(buffer.write(first.data(), first.size()), "first write failed");
    std::array<std::uint8_t, 3> output{};
    require(buffer.read(output.data(), 3) == 3, "first read size mismatch");
    require(output[0] == 1 && output[1] == 2 && output[2] == 3,
            "first read bytes mismatch");

    const std::array<std::uint8_t, 4> second{{5, 6, 7, 8}};
    require(buffer.write(second.data(), second.size()), "wraparound write failed");
    require(buffer.buffered_bytes() == 5, "buffer exceeded/underfilled capacity");
    std::array<std::uint8_t, 5> all{};
    require(buffer.read(all.data(), all.size()) == 5, "wrapped read size mismatch");
    const std::array<std::uint8_t, 5> expected{{4, 5, 6, 7, 8}};
    require(all == expected, "ring wraparound order mismatch");
}

void testProducerBackPressureAndCloseDrain() {
    std::mutex mutex;
    std::condition_variable condition;
    bool writer_blocked = false;
    yoinks::BoundedStreamBuffer buffer(3, [&] {
        std::lock_guard<std::mutex> lock(mutex);
        writer_blocked = true;
        condition.notify_all();
    });
    const std::vector<std::uint8_t> payload{10, 11, 12, 13, 14, 15, 16};
    bool writer_result = false;
    std::thread writer([&] {
        writer_result = buffer.write(payload.data(), payload.size());
        buffer.close();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return writer_blocked; });
    }

    std::vector<std::uint8_t> received;
    std::array<std::uint8_t, 2> part{};
    while (true) {
        const std::size_t count = buffer.read(part.data(), part.size());
        if (count == 0) break;
        received.insert(received.end(), part.begin(), part.begin() + static_cast<long>(count));
    }
    writer.join();
    require(writer_result, "writer should finish after consumer drains");
    require(received == payload, "back-pressure stream changed byte order/content");
    require(buffer.buffered_bytes() == 0, "closed buffer should be drained");
}

void testCancelWakesBlockedWriterAndReader() {
    std::mutex mutex;
    std::condition_variable condition;
    bool writer_blocked = false;
    yoinks::BoundedStreamBuffer buffer(1, [&] {
        std::lock_guard<std::mutex> lock(mutex);
        writer_blocked = true;
        condition.notify_all();
    });
    const std::uint8_t value = 42;
    require(buffer.write(&value, 1), "initial write failed");
    bool writer_result = true;
    std::thread writer([&] { writer_result = buffer.write(&value, 1); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return writer_blocked; });
    }
    buffer.cancel();
    writer.join();
    require(!writer_result, "cancelled writer must stop");
    std::uint8_t output = 0;
    require(buffer.read(&output, 1) == 0, "cancelled reader must observe EOF");
    require(buffer.buffered_bytes() == 0, "cancel must discard buffered data");
}

}  // namespace

int main() {
    try {
        testRejectsZeroCapacity();
        testWraparoundAndBoundedReads();
        testProducerBackPressureAndCloseDrain();
        testCancelWakesBlockedWriterAndReader();
        std::cout << "All bounded stream buffer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
