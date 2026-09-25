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

// What a Series 80 menu needs from the window server without redraw storing (Symbian OS 7.0s):
// - the part of a window another window stops covering is redrawn: the menu bar leaves out the title its
//   highlight window covers, so moving the highlight from File to Edit left File blank;
// - what a client drew outside a redraw stays: the menu pane un-highlights the item it leaves with one
//   non-redraw draw, and aging that segment out brought the old highlight back.

#include <services/window/classes/gstore.h>
#include <services/window/fifo.h>

#include <catch2/catch.hpp>

using namespace eka2l1;

namespace {
    common::region region_of(std::initializer_list<eka2l1::rect> rects) {
        common::region r;
        for (const eka2l1::rect &rect : rects) {
            r.add_rect(rect);
        }
        return r;
    }
}

TEST_CASE("uncovered_part_of_a_window_is_invalidated", "window_exposure") {
    // Menu bar at (5,5) 349x35; the title highlight window moves from (36,8) 36x29 to (72,8) 39x29.
    const eka2l1::rect bar({ 5, 5 }, { 349, 35 });
    common::region before = region_of({ bar });
    before.eliminate(eka2l1::rect({ 36, 8 }, { 36, 29 }));
    common::region after = region_of({ bar });
    after.eliminate(eka2l1::rect({ 72, 8 }, { 39, 29 }));

    const std::vector<eka2l1::rect> exposed = epoc::uncovered_window_rects(before, bar, after, bar);
    REQUIRE(exposed.size() >= 1);

    // Everything exposed is inside the File title's old place, in window coordinates (31,3) 36x29.
    common::region got;
    for (const eka2l1::rect &r : exposed) {
        got.add_rect(r);
    }

    const eka2l1::rect bounds = got.bounding_rect();
    REQUIRE(bounds.top == eka2l1::vec2(31, 3));
    REQUIRE(bounds.size == eka2l1::vec2(36, 29));
}

TEST_CASE("window_that_appeared_or_moved_is_not_invalidated_here", "window_exposure") {
    const eka2l1::rect win({ 10, 10 }, { 100, 50 });
    const common::region whole = region_of({ win });

    // Just became visible: its activation or visibility change invalidates it.
    REQUIRE(epoc::uncovered_window_rects(common::region(), eka2l1::rect(), whole, win).empty());

    // Moved: the store replays what it drew at the new place.
    const eka2l1::rect moved({ 20, 10 }, { 100, 50 });
    REQUIRE(epoc::uncovered_window_rects(whole, win, region_of({ moved }), moved).empty());

    // Unchanged.
    REQUIRE(epoc::uncovered_window_rects(whole, win, whole, win).empty());
}

TEST_CASE("aged_non_redraw_segment_is_kept_until_the_redraw", "window_exposure") {
    epoc::gdi_store_command_collection store;
    const eka2l1::rect full({ 0, 0 }, { 258, 110 });

    // The un-highlight, drawn outside a redraw long ago, and the current non-redraw segment, also aged.
    epoc::gdi_store_command_segment *unhighlight = store.add_new_segment(full, epoc::gdi_store_command_segment_non_redraw);
    unhighlight->creation_date_ = 0;
    store.redraw_done();
    epoc::gdi_store_command_segment *current = store.add_new_segment(full, epoc::gdi_store_command_segment_non_redraw);
    current->creation_date_ = 1;

    // Without redraw storing: the aged segment stays, marked, and the window is to be invalidated once.
    REQUIRE(store.clean_old_nonredraw_segments(true));
    REQUIRE(store.get_segments().size() == 2);
    REQUIRE(store.get_segments()[0]->superseded_);
    REQUIRE_FALSE(store.clean_old_nonredraw_segments(true));

    // The client's redraw of the whole window replaces both.
    store.add_new_segment(full, epoc::gdi_store_command_segment_pending_redraw);
    store.promote_last_segment();
    REQUIRE(store.get_segments().size() == 1);
    REQUIRE(store.get_segments()[0]->type_ == epoc::gdi_store_command_segment_redraw);
}

TEST_CASE("aged_non_redraw_segment_is_erased_with_redraw_storing", "window_exposure") {
    epoc::gdi_store_command_collection store;
    const eka2l1::rect full({ 0, 0 }, { 100, 100 });

    epoc::gdi_store_command_segment *old_seg = store.add_new_segment(full, epoc::gdi_store_command_segment_non_redraw);
    old_seg->creation_date_ = 0;
    store.redraw_done();
    epoc::gdi_store_command_segment *current = store.add_new_segment(full, epoc::gdi_store_command_segment_non_redraw);
    current->creation_date_ = 1;

    REQUIRE(store.clean_old_nonredraw_segments(false));
    REQUIRE(store.get_segments().size() == 1);
}

#include <drivers/graphics/graphics.h>
#include <drivers/itc.h>
#include <services/window/bitmap_cache.h>

namespace {
    // The last clip command the builder queued: {opcode, rect}.
    std::pair<std::uint32_t, eka2l1::rect> last_clip(drivers::graphics_command_builder &builder) {
        drivers::command_list list = builder.retrieve_command_list();
        std::pair<std::uint32_t, eka2l1::rect> found{ 0, eka2l1::rect() };

        for (std::size_t i = 0; i < list.size_; i++) {
            const drivers::command &cmd = list.base_[i];
            if ((cmd.opcode_ == drivers::graphics_driver_clip_bitmap_rect) || (cmd.opcode_ == drivers::graphics_driver_clip_region)) {
                found.first = cmd.opcode_;
                if (cmd.opcode_ == drivers::graphics_driver_clip_bitmap_rect) {
                    found.second = eka2l1::rect({ static_cast<int>(cmd.data_[0] & 0xFFFFFFFF), static_cast<int>(cmd.data_[0] >> 32) },
                        { static_cast<int>(cmd.data_[1] & 0xFFFFFFFF), static_cast<int>(cmd.data_[1] >> 32) });
                }
            }
        }

        delete[] list.base_;
        return found;
    }
}

TEST_CASE("clip_outside_the_segment_region_draws_nowhere", "window_exposure") {
    // A full redraw of the Sheet grid (523x146) that a later redraw of row 3 took over, except for the grid's
    // right border column: what the segment still owns.
    common::region owned;
    owned.add_rect(eka2l1::rect({ 0, 0 }, { 523, 79 }));
    owned.add_rect(eka2l1::rect({ 0, 108 }, { 523, 38 }));
    owned.add_rect(eka2l1::rect({ 0, 79 }, { 33, 29 }));
    owned.add_rect(eka2l1::rect({ 522, 79 }, { 1, 29 }));

    drivers::graphics_command_builder builder;
    epoc::bitmap_cache cache(nullptr);
    epoc::gdi_command_builder gdi(nullptr, builder, cache, drivers::filter_option::linear, { 0, 0 }, 1.0f, owned);

    // The column E cell of row 3 clips to (448,82) 74x24, all of it in the part the later redraw owns. The
    // cell's fill after it must not land on the border column the segment still owns.
    gdi.build_command_set_clip_rect_single(epoc::gdi_store_command_set_clip_rect_single_data{ eka2l1::rect({ 448, 82 }, { 74, 24 }) });

    const auto clip = last_clip(builder);
    REQUIRE(clip.first == drivers::graphics_driver_clip_bitmap_rect);
    REQUIRE(clip.second.size.x == 0);
    REQUIRE(clip.second.size.y == 0);
}
