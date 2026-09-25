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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>
#include <services/fbs/font_atlas.h>

using namespace eka2l1;

// A TrueType glyph rasterised again at the display scale must not move the pen by its own (re-hinted, re-rounded)
// advance: the guest measured the text with the 1x advances FBS gave it, and put the caret there.
TEST_CASE("font_atlas_pen_uses_client_advance_scaled", "fbs") {
    const int client = 6; // 1x advance the guest got (6.4 rounded)
    REQUIRE(epoc::font_atlas::pen_advance(12.8f, 1.0f, &client, 2.0f) == 12);

    // Five of them ("hello"): 60 px, where the scaled font's own advances would give 65 - the caret on the "o".
    int pen = 0, own = 0;
    for (int i = 0; i < 5; i++) {
        pen += epoc::font_atlas::pen_advance(12.8f, 1.0f, &client, 2.0f);
        own += epoc::font_atlas::pen_advance(12.8f, 1.0f, nullptr, 0.0f);
    }
    REQUIRE(pen == 60);
    REQUIRE(own == 65);
}

TEST_CASE("font_atlas_pen_without_client_advance", "fbs") {
    // A bitmap (GDR) font is drawn from its 1x atlas scaled up: the atlas advance times the scale, as before.
    REQUIRE(epoc::font_atlas::pen_advance(7.0f, 2.0f, nullptr, 0.0f) == 14);
    const int client = 9;
    REQUIRE(epoc::font_atlas::pen_advance(7.0f, 2.0f, &client, 0.0f) == 14);
}

TEST_CASE("font_atlas_client_advance_store", "fbs") {
    epoc::font_atlas atlas;
    int adv = 0;
    REQUIRE_FALSE(atlas.get_client_advance('h', adv));
    atlas.set_client_advance('h', 7);
    REQUIRE(atlas.get_client_advance('h', adv));
    REQUIRE(adv == 7);
}
