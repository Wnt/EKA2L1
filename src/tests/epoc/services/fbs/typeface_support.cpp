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

// How the font store answers the typeface and height enumeration clients choose fonts with. Series 80 Sheet
// walks TypefaceSupport and FontHeightInTwips for its 10 pt cell font: with one height reported for an Open
// Font typeface it settled on 4 pt, and a twips height truncated to pixels picked the wrong bitmap size.

#include <catch2/catch.hpp>

#include <services/fbs/font_store.h>
#include <services/window/common.h>

using namespace eka2l1;

TEST_CASE("open_font_standard_sizes_follow_fntstore", "fbs") {
    const std::vector<std::int32_t> &sizes = epoc::font_store::open_font_standard_sizes_in_twips();

    // 4-18 pt by 1, 20-36 by 2, 40-72 by 4, 80-144 by 8.
    REQUIRE(sizes.size() == 42);
    REQUIRE(sizes.front() == 80);
    REQUIRE(sizes[6] == 200);
    REQUIRE(sizes[15] == 400);
    REQUIRE(sizes.back() == 2880);

    // The first standard size at or above the minimum.
    REQUIRE(epoc::font_store::open_font_nearest_size_index(79) == 0);
    REQUIRE(epoc::font_store::open_font_nearest_size_index(80) == 0);
    REQUIRE(epoc::font_store::open_font_nearest_size_index(81) == 1);
    REQUIRE(epoc::font_store::open_font_nearest_size_index(3000) == sizes.size());
}

TEST_CASE("series80_twips_round_to_the_nearest_pixel", "fbs") {
    // FNTSTORE VerticalTwipsToPixels with 9780 per mille: Sheet's formula-bar font is 176 twips = 18 px.
    REQUIRE(epoc::twips_to_pixels(epocver::epoc7, 176) == 18);
    REQUIRE(epoc::twips_to_pixels(epocver::epoc7, 200) == 20);
    REQUIRE(epoc::twips_to_pixels(epocver::epoc7, 80) == 8);
    REQUIRE(epoc::twips_to_pixels(epocver::epoc7, 0) == 0);

    // Every pixel height handed out in twips converts back to itself.
    for (std::int32_t pixels = 1; pixels <= 256; pixels++) {
        CAPTURE(pixels);
        REQUIRE(epoc::twips_to_pixels(epocver::epoc7, epoc::pixels_to_twips(epocver::epoc7, pixels)) == pixels);
    }

    // Whole ratios keep the historical division.
    REQUIRE(epoc::twips_to_pixels(epocver::epoc80, 239) == 15);
    REQUIRE(epoc::twips_to_pixels(epocver::epoc94, 144) == 16);
}
