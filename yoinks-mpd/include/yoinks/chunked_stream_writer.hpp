/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

namespace yoinks {

// Writes HTTP/1.1 chunk framing around a bounded pull-reader. The reader
// supplies at most 16 KiB per call; the terminal chunk is emitted only when
// the caller confirms successful upstream completion.
template <typename Read, typename IsComplete, typename Write>
bool writeChunkedStream(Read read, IsComplete is_complete, Write write) {
    std::array<std::uint8_t, 16 * 1024> block{};
    for (;;) {
        const std::size_t count = read(block.data(), block.size());
        if (count == 0) break;
        if (count > block.size()) return false;

        std::ostringstream line;
        line << std::hex << count << "\r\n";
        const std::string chunk_header = line.str();
        if (!write(chunk_header.data(), chunk_header.size())) return false;
        if (!write(reinterpret_cast<const char*>(block.data()), count)) return false;
        if (!write("\r\n", 2)) return false;
    }

    if (!is_complete()) return false;
    return write("0\r\n\r\n", 5);
}

}  // namespace yoinks
