/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/bounded_stream_buffer.hpp"

#include <algorithm>
#include <stdexcept>

namespace yoinks {

BoundedStreamBuffer::BoundedStreamBuffer(
        const std::size_t capacity,
        std::function<void()> before_writer_wait_for_tests)
    : capacity_(capacity), storage_(capacity),
      before_writer_wait_for_tests_(std::move(before_writer_wait_for_tests)) {
    if (capacity == 0) {
        throw std::invalid_argument("stream buffer capacity must be positive");
    }
}

bool BoundedStreamBuffer::write(const std::uint8_t* data, const std::size_t size) {
    if (size != 0 && data == nullptr) {
        return false;
    }

    std::size_t written = 0;
    std::unique_lock<std::mutex> lock(mutex_);
    while (written < size) {
        if (size_ == capacity_ && before_writer_wait_for_tests_) {
            // Test callback runs under mutex_: it may signal an external
            // condition, but must never call back into this buffer.
            before_writer_wait_for_tests_();
        }
        writable_.wait(lock, [this] {
            return size_ < capacity_ || closed_ || cancelled_;
        });
        if (closed_ || cancelled_) {
            return false;
        }

        const std::size_t available = capacity_ - size_;
        const std::size_t chunk = std::min(size - written, available);
        const std::size_t first = std::min(chunk, capacity_ - tail_);
        std::copy_n(data + written, first, storage_.data() + tail_);
        const std::size_t second = chunk - first;
        if (second != 0) {
            std::copy_n(data + written + first, second, storage_.data());
        }
        tail_ = (tail_ + chunk) % capacity_;
        size_ += chunk;
        written += chunk;
        readable_.notify_one();
    }
    return true;
}

std::size_t BoundedStreamBuffer::read(std::uint8_t* destination,
                                      const std::size_t maximum) {
    if (maximum == 0 || destination == nullptr) {
        return 0;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    readable_.wait(lock, [this] {
        return size_ != 0 || closed_ || cancelled_;
    });
    if (cancelled_ || size_ == 0) {
        return 0;
    }

    const std::size_t chunk = std::min(maximum, size_);
    const std::size_t first = std::min(chunk, capacity_ - head_);
    std::copy_n(storage_.data() + head_, first, destination);
    const std::size_t second = chunk - first;
    if (second != 0) {
        std::copy_n(storage_.data(), second, destination + first);
    }
    head_ = (head_ + chunk) % capacity_;
    size_ -= chunk;
    writable_.notify_all();
    return chunk;
}

void BoundedStreamBuffer::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    readable_.notify_all();
    writable_.notify_all();
}

void BoundedStreamBuffer::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
        size_ = 0;
        head_ = tail_;
    }
    readable_.notify_all();
    writable_.notify_all();
}

std::size_t BoundedStreamBuffer::capacity() const noexcept {
    return capacity_;
}

std::size_t BoundedStreamBuffer::buffered_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

}  // namespace yoinks
