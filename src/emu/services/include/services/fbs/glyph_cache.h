// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace eka2l1::epoc {
    // Clients identify a font by its COpenFont address, which the shared heap
    // can reuse immediately. Remove matching entries, including collision slots,
    // before releasing that font's storage.
    template <typename Entry, typename Resolve, typename Release>
    void discard_pointer_font_glyphs(std::int32_t *slots, std::int32_t count,
        std::uint32_t font_address, Resolve resolve, Release release) {
        for (std::int32_t i = 0; i < count; ++i) {
            if (!slots[i]) continue;
            Entry *entry = resolve(static_cast<std::uint32_t>(slots[i]));
            if (entry && static_cast<std::uint32_t>(entry->font_offset) == font_address) {
                slots[i] = 0;
                release(entry);
            }
        }
    }
}
