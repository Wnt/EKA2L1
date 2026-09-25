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

// CGraphicsContext::TDrawMode emulated with blend passes. Series 80 Sheet draws and erases its cell
// cursor with EDrawModeNOTSCREEN, so a draw mode that is ignored leaves every visited cell highlighted.
// Every mode is checked against the bitwise definition (screen driver: invert the pen and/or the screen
// operand, then AND/OR/XOR) on the channel values 0 and 255, where the blend passes must be exact.

#include <services/window/classes/gstore.h>

#include <catch2/catch.hpp>

#include <cstdint>

using namespace eka2l1;

namespace {
    float factor_value(const drivers::blend_factor factor, const float src, const float dst) {
        switch (factor) {
        case drivers::blend_factor::one:
            return 1.0f;
        case drivers::blend_factor::zero:
            return 0.0f;
        case drivers::blend_factor::frag_out_color:
            return src;
        case drivers::blend_factor::one_minus_frag_out_color:
            return 1.0f - src;
        case drivers::blend_factor::current_color:
            return dst;
        case drivers::blend_factor::one_minus_current_color:
            return 1.0f - dst;
        default:
            FAIL("unexpected blend factor");
            return 0.0f;
        }
    }

    // One channel through the modes' blend passes (or a plain replace when there are none).
    int apply_mode(const std::uint32_t mode, const int pen, const int screen) {
        eka2l1::vec4 color(pen, pen, pen, 255);
        epoc::gdi_draw_mode_pass passes[2];
        const std::uint32_t count = epoc::gdi_expand_draw_mode(mode, color, passes);

        if (count == 0) {
            return color.x;
        }

        float dst = screen / 255.0f;
        for (std::uint32_t i = 0; i < count; i++) {
            const float src = passes[i].color_.x / 255.0f;
            dst = src * factor_value(passes[i].src_factor_, src, dst) + dst * factor_value(passes[i].dst_factor_, src, dst);
        }

        return static_cast<int>(dst * 255.0f + 0.5f);
    }

    int reference(const std::uint32_t mode, const int pen, const int screen) {
        int p = (mode & epoc::gdi_draw_mode_invert_pen) ? (255 - pen) : pen;
        int s = (mode & epoc::gdi_draw_mode_invert_screen) ? (255 - screen) : screen;

        switch (mode & epoc::gdi_draw_mode_logical_op) {
        case epoc::gdi_draw_mode_xor:
            return p ^ s;
        case epoc::gdi_draw_mode_and:
            return p & s;
        case epoc::gdi_draw_mode_or:
            return p | s;
        default:
            // No logical op: NOTSCREEN inverts the screen, PEN/NOTPEN write the pen.
            return (mode & epoc::gdi_draw_mode_pen) ? p : s;
        }
    }
}

TEST_CASE("draw_mode_passes_match_bitwise_ops", "draw_mode") {
    // TDrawMode values from GDI.H.
    const std::uint32_t modes[] = {
        1,  // EDrawModeNOTSCREEN
        2,  // EDrawModeXOR
        3,  // EDrawModeNOTXOR
        4,  // EDrawModeOR
        5,  // EDrawModeNOTOR
        8,  // EDrawModeAND
        9,  // EDrawModeNOTAND
        20, // EDrawModeORNOT
        21, // EDrawModeNOTORNOT
        24, // EDrawModeANDNOT
        25, // EDrawModeNOTANDNOT
        32, // EDrawModePEN
        48  // EDrawModeNOTPEN
    };

    for (const std::uint32_t mode : modes) {
        for (const int pen : { 0, 255 }) {
            for (const int screen : { 0, 255 }) {
                INFO("mode " << mode << " pen " << pen << " screen " << screen);
                REQUIRE(apply_mode(mode, pen, screen) == reference(mode, pen, screen));
            }
        }
    }
}

TEST_CASE("draw_mode_notscreen_twice_restores", "draw_mode") {
    // Sheet's cursor: drawn with NOTSCREEN, erased by drawing the same bars with NOTSCREEN again.
    for (const int screen : { 0, 64, 200, 255 }) {
        const int once = apply_mode(epoc::gdi_draw_mode_notscreen, 0, screen);
        REQUIRE(once == 255 - screen);
        REQUIRE(apply_mode(epoc::gdi_draw_mode_notscreen, 0, once) == screen);
    }
}

TEST_CASE("draw_mode_plain_modes_need_no_blending", "draw_mode") {
    eka2l1::vec4 color(10, 20, 30, 255);
    epoc::gdi_draw_mode_pass passes[2];

    REQUIRE(epoc::gdi_expand_draw_mode(epoc::gdi_draw_mode_pen, color, passes) == 0);
    REQUIRE(color == eka2l1::vec4(10, 20, 30, 255));

    REQUIRE(epoc::gdi_expand_draw_mode(epoc::gdi_draw_mode_notpen, color, passes) == 0);
    REQUIRE(color == eka2l1::vec4(245, 235, 225, 255));
}
