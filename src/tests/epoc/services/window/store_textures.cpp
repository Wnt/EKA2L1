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

// The redraw store replays a window's drawing on every recomposition. A blit must replay the pixels
// blitted then, not what the bitmap holds by the time of the replay (an S80 list box and the dialog
// over it paint their highlighted rows through one shared off-screen bitmap). So a stored blit holds
// its texture in the bitmap cache for as long as its segment lives.

#include <services/window/bitmap_cache.h>
#include <services/window/classes/gstore.h>

#include <catch2/catch.hpp>

#include <memory>

using namespace eka2l1;

namespace {
    epoc::gdi_store_command blit(const drivers::handle main, const drivers::handle mask) {
        epoc::gdi_store_command command;
        command.opcode_ = epoc::gdi_store_command_draw_bitmap;

        auto &data = command.get_data_struct<epoc::gdi_store_command_draw_bitmap_data>();
        // Raw bitwise bitmaps: the segment takes no FBS reference on them.
        data.gdi_flags_ = epoc::GDI_STORE_COMMAND_MAIN_RAW | epoc::GDI_STORE_COMMAND_MASK_RAW;
        data.main_drv_ = main;
        data.mask_drv_ = mask;
        return command;
    }
}

TEST_CASE("A stored blit holds its textures until its segment goes", "[gdi_store]") {
    epoc::bitmap_cache cache(nullptr);

    auto segment = std::make_unique<epoc::gdi_store_command_segment>();
    auto first = blit(7, 9);
    auto again = blit(7, 0);
    segment->add_command(first, &cache);
    segment->add_command(again, &cache);

    // One hold per texture and segment, however many blits use it.
    REQUIRE(cache.store_held_count() == 2);

    auto other = std::make_unique<epoc::gdi_store_command_segment>();
    auto third = blit(7, 0);
    other->add_command(third, &cache);

    segment.reset();
    REQUIRE(cache.store_held_count() == 1);

    other.reset();
    REQUIRE(cache.store_held_count() == 0);
    REQUIRE(cache.orphan_count() == 0);
}

TEST_CASE("A blit recorded without the cache holds nothing", "[gdi_store]") {
    epoc::bitmap_cache cache(nullptr);

    {
        epoc::gdi_store_command_segment segment;
        auto command = blit(3, 0);
        segment.add_command(command);
    }

    REQUIRE(cache.store_held_count() == 0);

    // Releasing a texture nothing holds is harmless.
    cache.release(3);
    REQUIRE(cache.store_held_count() == 0);
}
