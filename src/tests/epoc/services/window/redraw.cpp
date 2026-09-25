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

// WSERV never reports a redraw for the part of a window that its children cover. Series 80 Contacts'
// New card has an outer window with the handle (this | 1) fully covered by an inner child window; a
// redraw for the outer one reaches CONE with an odd handle and panics CONE 46 (ECoePanicInvalidHandle).

#include <services/window/fifo.h>

#include <catch2/catch.hpp>

using namespace eka2l1;

namespace {
    epoc::redraw_event redraw(const std::uint32_t handle, const eka2l1::vec2 &tl, const eka2l1::vec2 &br) {
        return epoc::redraw_event{ handle, tl, br };
    }
}

TEST_CASE("redraw_covered_by_child_window", "window_redraw") {
    // Outer window at (4,34) size 484x162, inner child at the same origin, 484x390 (the New card layout).
    const eka2l1::vec2 outer_top(4, 34);
    const std::vector<eka2l1::rect> child{ eka2l1::rect({ 4, 34 }, { 484, 390 }) };

    REQUIRE(epoc::redraw_covered_by(redraw(0x72D2ED, { 0, 0 }, { 484, 162 }), outer_top, child));
    REQUIRE(epoc::redraw_covered_by(redraw(0x72D2ED, { 10, 10 }, { 20, 20 }), outer_top, child));
}

TEST_CASE("redraw_partly_uncovered_is_reported", "window_redraw") {
    const eka2l1::vec2 outer_top(4, 34);
    const std::vector<eka2l1::rect> child{ eka2l1::rect({ 4, 34 }, { 484, 100 }) };

    REQUIRE_FALSE(epoc::redraw_covered_by(redraw(1, { 0, 0 }, { 484, 162 }), outer_top, child));
    REQUIRE_FALSE(epoc::redraw_covered_by(redraw(1, { 0, 0 }, { 484, 162 }), outer_top, {}));
}

TEST_CASE("redraw_covered_by_two_children", "window_redraw") {
    const std::vector<eka2l1::rect> halves{ eka2l1::rect({ 0, 0 }, { 50, 100 }), eka2l1::rect({ 50, 0 }, { 50, 100 }) };
    REQUIRE(epoc::redraw_covered_by(redraw(1, { 0, 0 }, { 100, 100 }), { 0, 0 }, halves));
}

TEST_CASE("redraw_fifo_drops_hidden_redraws", "window_redraw") {
    epoc::redraw_fifo fifo;
    int outer = 0, inner = 0;

    fifo.queue_event(&outer, redraw(0x101, { 0, 0 }, { 10, 10 }), 1);
    fifo.queue_event(&inner, redraw(0x100, { 0, 0 }, { 10, 10 }), 1);

    auto evt = fifo.get_visible_evt_opt([&](const epoc::redraw_event_full &e) { return e.owner_ == &outer; });
    REQUIRE(evt.has_value());
    REQUIRE(evt->evt_.handle == 0x100);

    REQUIRE_FALSE(fifo.get_visible_evt_opt(nullptr).has_value());
}
