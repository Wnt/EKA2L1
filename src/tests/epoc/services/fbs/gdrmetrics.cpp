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

// The metrics a GDR bitmap font is handed to clients with. Series 80 Desk sizes its icon label boxes and
// places their baselines from these; with the whole cell reported as TOpenFontMetrics::iMaxHeight the
// box came out 4 px taller than on the device and the label 8 px low.

#include <catch2/catch.hpp>

#include <services/fbs/adapter/gdr_font_adapter.h>
#include <services/fbs/font.h>

using namespace eka2l1;

TEST_CASE("gdr_metrics_split_the_cell_at_the_baseline", "fbs") {
    // linda.gdr System bold 20 on the Nokia 9300: a 20-pixel cell with the baseline 16 pixels down.
    loader::gdr::font_bitmap_header header{};
    header.cell_height_in_pixels_ = 20;
    header.ascent_in_pixels_ = 16;
    header.max_char_width_in_pixels_ = 19;

    const epoc::open_font_metrics metrics = epoc::adapter::make_gdr_font_metrics(header);

    REQUIRE(metrics.design_height == 20);
    REQUIRE(metrics.ascent == 16);
    REQUIRE(metrics.descent == 4);
    REQUIRE(metrics.max_height == 16);
    REQUIRE(metrics.max_depth == 4);
    REQUIRE(metrics.max_width == 19);
    REQUIRE(epoc::font_height_in_pixels(metrics, false) == 20);
}

TEST_CASE("scalable_font_height_is_its_max_height", "fbs") {
    epoc::open_font_metrics metrics{};
    metrics.design_height = 12;
    metrics.max_height = 15;

    REQUIRE(epoc::font_height_in_pixels(metrics, true) == 15);
}
