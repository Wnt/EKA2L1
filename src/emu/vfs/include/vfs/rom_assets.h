// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace eka2l1 {
    // A ROM MBM may be the whole file or an embedded store in an AIF.
    // Validate the offset table and object headers before publishing an address.
    inline bool contains_rom_bitmap_store(const std::vector<std::uint8_t> &bytes) {
        auto word = [&](std::size_t off) {
            std::uint32_t value;
            std::memcpy(&value, bytes.data() + off, 4);
            return value;
        };
        for (std::size_t base = 0; base + 12 <= bytes.size(); base += 4) {
            if (word(base) != 0x10000041) continue;
            const auto count = word(base + 4);
            if (!count || count > (bytes.size() - base - 8) / 4) continue;
            bool valid = true;
            for (std::size_t i = 0; i < count; ++i) {
                const auto offset = word(base + 8 + i * 4);
                if (offset < 8 + count * 4 || offset > bytes.size() - base
                    || bytes.size() - base - offset < 68) { valid = false; break; }
                const std::size_t object = base + offset;
                const auto data = word(object + 64);
                if (word(object) != 0x10000040 || word(object + 24) != 40
                    || data < 68 || data > bytes.size() - object) { valid = false; break; }
            }
            if (valid) return true;
        }
        return false;
    }
}
