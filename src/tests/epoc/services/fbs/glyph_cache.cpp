// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#include <services/fbs/font.h>
#include <services/fbs/glyph_cache.h>
#include <catch2/catch.hpp>
#include <array>

TEMPLATE_TEST_CASE("Released pointer-ABI fonts lose cached glyphs before address reuse", "fbs",
    eka2l1::epoc::open_font_session_cache_entry_v1,
    eka2l1::epoc::open_font_session_cache_entry_v2) {
    std::array<TestType, 3> entries{};
    entries[0].font_offset = entries[2].font_offset = 0x81234000;
    entries[1].font_offset = 0x81235000;
    std::int32_t slots[] = {1, 0, 2, 3}; // Includes displaced collision slots.
    unsigned released = 0;
    const auto purge = [&] {
        eka2l1::epoc::discard_pointer_font_glyphs<TestType>(slots, 4, 0x81234000,
            [&](std::uint32_t address) { return &entries.at(address - 1); },
            [&](TestType *entry) {
                REQUIRE((entry == &entries[0] || entry == &entries[2]));
                ++released;
            });
    };
    purge();
    REQUIRE(released == 2);
    REQUIRE(slots[0] == 0);
    REQUIRE(slots[1] == 0);
    REQUIRE(slots[2] == 2); // Unrelated live font retains its glyph.
    REQUIRE(slots[3] == 0);
    purge();
    REQUIRE(released == 2); // Repeated invalidation cannot double-free.
}
