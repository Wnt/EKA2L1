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

// CFbsBitGc::DrawRect and DrawLine geometry. Series 80 Sheet draws and erases its cell cursor as bars in
// EDrawModeNOTSCREEN; a pixel painted twice flips back, so a rectangle's fill and outline must not overlap.
// Its formula bar frames itself with lines that run right-to-left and bottom-to-top.

#include <services/window/classes/gstore.h>

#include <catch2/catch.hpp>

#include <set>
#include <utility>

using namespace eka2l1;

namespace {
    // Every pixel painted by the split, with a count per pixel.
    std::multiset<std::pair<int, int>> painted(const epoc::gdi_rect_outline &outline) {
        std::multiset<std::pair<int, int>> pixels;
        auto add = [&](const eka2l1::rect &r) {
            for (int y = r.top.y; y < r.top.y + r.size.y; y++) {
                for (int x = r.top.x; x < r.top.x + r.size.x; x++) {
                    pixels.insert({ x, y });
                }
            }
        };

        if (outline.has_fill) {
            add(outline.fill);
        }
        for (std::size_t i = 0; i < outline.edge_count; i++) {
            add(outline.edges[i]);
        }

        return pixels;
    }
}

TEST_CASE("draw_rect_paints_each_pixel_once_with_a_one_pixel_pen", "[window]") {
    // Sheet's cursor bar [127,28)-(130,52): brush plus the default pen.
    const eka2l1::rect bar({ 127, 28 }, { 3, 24 });
    const epoc::gdi_rect_outline outline = epoc::gdi_split_rect_outline(bar, { 1, 1 });
    const auto pixels = painted(outline);

    REQUIRE(pixels.size() == 3 * 24);
    for (int y = 28; y < 52; y++) {
        for (int x = 127; x < 130; x++) {
            CAPTURE(x, y);
            REQUIRE(pixels.count({ x, y }) == 1);
        }
    }

    // The pen stays inside the rectangle: nothing on its right or bottom edge coordinate.
    REQUIRE(pixels.count({ 130, 28 }) == 0);
    REQUIRE(pixels.count({ 127, 52 }) == 0);

    // The interior is the brush's.
    REQUIRE(outline.has_fill);
    REQUIRE(outline.fill.top == eka2l1::vec2(128, 29));
    REQUIRE(outline.fill.size == eka2l1::vec2(1, 22));
}

TEST_CASE("draw_rect_thin_rectangles_have_no_fill_and_no_overlap", "[window]") {
    // The 1-pixel top bar of the cursor.
    const epoc::gdi_rect_outline one_row = epoc::gdi_split_rect_outline(eka2l1::rect({ 26, 27 }, { 104, 1 }), { 1, 1 });
    REQUIRE_FALSE(one_row.has_fill);
    const auto row_pixels = painted(one_row);
    REQUIRE(row_pixels.size() == 104);
    REQUIRE(std::set<std::pair<int, int>>(row_pixels.begin(), row_pixels.end()).size() == 104);

    // Two pixels high: top and bottom edge, nothing in between.
    const auto two = painted(epoc::gdi_split_rect_outline(eka2l1::rect({ 0, 0 }, { 5, 2 }), { 1, 1 }));
    REQUIRE(two.size() == 10);
    REQUIRE(std::set<std::pair<int, int>>(two.begin(), two.end()).size() == 10);
}

TEST_CASE("draw_rect_without_a_pen_fills_the_whole_rectangle", "[window]") {
    const eka2l1::rect area({ 1, 1 }, { 400, 19 });
    const epoc::gdi_rect_outline outline = epoc::gdi_split_rect_outline(area, { 0, 0 });

    REQUIRE(outline.has_fill);
    REQUIRE(outline.fill.top == area.top);
    REQUIRE(outline.fill.size == area.size);
    REQUIRE(outline.edge_count == 0);
}

TEST_CASE("draw_rect_wide_pen_is_centred_on_the_edge", "[window]") {
    // A 3-pixel pen: one pixel outside the rectangle's edge, two inside.
    const eka2l1::rect area({ 10, 10 }, { 10, 10 });
    const epoc::gdi_rect_outline outline = epoc::gdi_split_rect_outline(area, { 3, 3 });
    const auto pixels = painted(outline);

    REQUIRE(pixels.count({ 9, 9 }) == 1);
    REQUIRE(pixels.count({ 11, 11 }) == 1);
    REQUIRE(pixels.count({ 12, 12 }) == 1);
    REQUIRE(pixels.count({ 8, 8 }) == 0);
    REQUIRE(outline.fill.top == eka2l1::vec2(12, 12));
    REQUIRE(outline.fill.size == eka2l1::vec2(6, 6));

    // No pixel twice.
    REQUIRE(std::set<std::pair<int, int>>(pixels.begin(), pixels.end()).size() == pixels.size());
}

TEST_CASE("draw_line_excludes_the_end_point_in_either_direction", "[window]") {
    // The formula bar's frame: DrawLine((0,0), (402,0)), ((0,22), (0,-1)), ((401,22), (-1,22)).
    const eka2l1::rect top = epoc::gdi_axis_line_rect({ 0, 0 }, { 402, 0 }, { 1, 1 });
    REQUIRE(top.top == eka2l1::vec2(0, 0));
    REQUIRE(top.size == eka2l1::vec2(402, 1));

    const eka2l1::rect left = epoc::gdi_axis_line_rect({ 0, 22 }, { 0, -1 }, { 1, 1 });
    REQUIRE(left.top == eka2l1::vec2(0, 0));
    REQUIRE(left.size == eka2l1::vec2(1, 23));

    const eka2l1::rect bottom = epoc::gdi_axis_line_rect({ 401, 22 }, { -1, 22 }, { 1, 1 });
    REQUIRE(bottom.top == eka2l1::vec2(0, 22));
    REQUIRE(bottom.size == eka2l1::vec2(402, 1));

    const eka2l1::rect down = epoc::gdi_axis_line_rect({ 5, 3 }, { 5, 8 }, { 1, 1 });
    REQUIRE(down.top == eka2l1::vec2(5, 3));
    REQUIRE(down.size == eka2l1::vec2(1, 5));
}

TEST_CASE("draw_line_centres_a_wide_pen_and_plots_a_point", "[window]") {
    const eka2l1::rect wide = epoc::gdi_axis_line_rect({ 10, 10 }, { 20, 10 }, { 3, 3 });
    REQUIRE(wide.top == eka2l1::vec2(9, 9));
    REQUIRE(wide.size == eka2l1::vec2(12, 3));

    const eka2l1::rect dot = epoc::gdi_axis_line_rect({ 4, 4 }, { 4, 4 }, { 1, 1 });
    REQUIRE(dot.top == eka2l1::vec2(4, 4));
    REQUIRE(dot.size == eka2l1::vec2(1, 1));
}
