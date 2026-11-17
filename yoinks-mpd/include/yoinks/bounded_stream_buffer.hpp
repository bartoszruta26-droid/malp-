/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace yoinks {

/**
 * Fixed-capacity in-memory byte queue for progressive HTTP relay.
 *
 * The backing store is allocated once at construction. This type performs no
 * file I/O, creates no temporary files and never grows beyond capacity.
 * write() blocks under back-pressure; read() returns available bytes as soon
 * as at least one byte is present. close() drains buffered bytes before EOF.
 * cancel() discards buffered bytes and wakes all waiters immediately.
 */
class BoundedStreamBuffer final {
public:
    explicit BoundedStreamBuffer(std::size_t capacity);
    BoundedStreamBuffer(const BoundedStreamBuffer&) = delete;
    BoundedStreamBuffer& operator=(const BoundedStreamBuffer&) = delete;

    // Returns false if cancelled/closed before all bytes could be accepted.
    bool write(const std::uint8_t* data, std::size_t size);

    // Returns 0 only after close/cancel and all readable bytes are consumed.
    // A zero maximum is a no-op and returns 0.
    std::size_t read(std::uint8_t* destination, std::size_t maximum);

    // Producer EOF: consumers may drain existing bytes and then receive 0.
    void close();

    // Abort: discard pending bytes and wake blocked readers/writers.
    void cancel();

    std::size_t capacity() const noexcept;
    std::size_t buffered_bytes() const;

private:
    const std::size_t capacity_;
    std::vector<std::uint8_t> storage_;
    mutable std::mutex mutex_;
    std::condition_variable readable_;
    std::condition_variable writable_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
    bool closed_{false};
    bool cancelled_{false};
};

}  // namespace yoinks
