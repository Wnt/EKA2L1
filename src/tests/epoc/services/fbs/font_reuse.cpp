/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// When FBS hands out a font object it already made. Series 80 Documents asks for 'System' 15 px again and
// again; linda.gdr has no 15 px System, so the request resolves to the 10 px bitmap. The reuse test compared
// the requested 15 with the bitmap's 10 and made a new font object for every request.

#include <catch2/catch.hpp>

#include <services/fbs/fbs.h>

using namespace eka2l1;

TEST_CASE("bitmap_font_request_reuses_the_bitmap_it_resolves_to", "fbs") {
    // 'System' 15 -> bitmap index 7 (the 10 px one); the object made for it is held.
    REQUIRE(fbs_font_object_reusable(false, 7, 7, 0, 0, 0));
    // Another bitmap of the typeface (the bold 10 px, or the 16 px) is another font.
    REQUIRE_FALSE(fbs_font_object_reusable(false, 7, 8, 0, 0, 0));
}

TEST_CASE("font_request_with_another_style_gets_its_own_object", "fbs") {
    const std::uint32_t bold = epoc::font_style_base::bold;
    const std::uint32_t sub = epoc::font_style_base::sub;

    // A bold request that falls back to the regular bitmap still reads bold back from its spec.
    REQUIRE_FALSE(fbs_font_object_reusable(false, 7, 7, 0, bold, 0));
    // A subscript request carries its own algorithmic baseline.
    REQUIRE_FALSE(fbs_font_object_reusable(true, 20, 20, 0, sub, 0));
    REQUIRE(fbs_font_object_reusable(false, 7, 7, bold, bold, 0));
}

TEST_CASE("scalable_font_request_reuses_a_size_within_one_pixel", "fbs") {
    REQUIRE(fbs_font_object_reusable(true, 20, 21, 0, 0, 1));
    REQUIRE(fbs_font_object_reusable(true, 20, 20, 0, 0, 0));
    REQUIRE_FALSE(fbs_font_object_reusable(true, 20, 22, 0, 0, 2));
}
