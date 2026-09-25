/*
 * Copyright (c) 2026 EKA2L1 Team
 *
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
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

#pragma once

#include <common/region.h>
#include <common/vecx.h>
#include <drivers/graphics/common.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eka2l1 {
    class window_server;
    struct fbsfont;

    namespace drivers {
        class graphics_driver;
        class graphics_command_builder;
    }

    namespace kernel {
        class process;
    }
}

namespace eka2l1::epoc {
    struct screen;
    struct window_group;

    /**
     * \brief The Series 80 v2 status pane, drawn by the window server when the Eikon server is the HLE one.
     *
     * On the device the ROM Eikon server (EikSrvUi) owns a window group named "EiksrvBackdrop" with one
     * window, and keeps that group directly behind the foreground application's group. Its window takes
     * the rectangle the application's status pane layout leaves for it: the lower left pane under the
     * application icon (0,100 92x100) for R_BSKN_INDICATOR_LAYOUT_WIDE (Desk, Telephone), the full-height
     * strip left of the title bar (0,0 32x200) for R_BSKN_INDICATOR_LAYOUT (Documents, Sheet, Web). It
     * paints the skin's application view background there, then the network and battery indicators, and
     * in the wide layout a digital clock (hours:minutes, redrawn when the minute changes). (Window tree and pixels taken from the ROM server running under
     * EKA2L1_ROM_EIKSRV=1.) Apps send the layout with EikSrv op 7 (SetStatusPaneLayout) when they come to
     * the foreground.
     *
     * Without the ROM server nothing painted that rectangle, so the application behind the foreground
     * one showed through. This stands in for the backdrop: the HLE EikAppUiServer records each
     * application's layout; the screen treats the pane as an opaque layer right behind the frontmost
     * group that sent one (older applications' windows lose that area from their visible regions), and
     * paints it on each full redraw.
     */
    class s80_status_pane {
    public:
        enum layout_kind {
            layout_none = 0,
            layout_narrow = 1,
            layout_wide = 2
        };

    private:
        struct image {
            drivers::handle handle_ = 0;
            eka2l1::vec2 size_{ 0, 0 };
            bool tried_ = false;
        };

        window_server *serv_;
        std::map<std::uint64_t, std::uint32_t> layouts_; ///< Process unique id -> layout resource id.

        image background_narrow_;
        image background_wide_;
        image network_;
        image battery_;

        bool load_image(drivers::graphics_driver *driver, image &img, const std::u16string &path, const int index,
            const bool key_magenta);

        fbsfont *clock_font_ = nullptr;
        std::int64_t shown_minute_ = -1;
        bool shown_twelve_hour_ = false;

        fbsfont *clock_font();
        fbsfont *am_pm_font();
        bool twelve_hour_clock() const;
        std::int64_t local_seconds() const;
        void draw_clock(drivers::graphics_command_builder &builder, screen *scr, const common::region &visible);

    public:
        explicit s80_status_pane(window_server *serv);
        ~s80_status_pane();

        window_server *get_window_server() const {
            return serv_;
        }

        /**
         * \brief Whether the pane shows a clock whose minute has changed since it was last drawn.
         */
        bool clock_changed(const layout_kind kind);

        /**
         * \brief Record the layout an application asked for (EikSrv op 7 argument 0, a BASESKIN resource id).
         */
        void set_layout(kernel::process *pr, const std::uint32_t layout_res);

        /**
         * \brief The pane layout of the application owning this window group, or layout_none.
         */
        layout_kind layout_of(window_group *group) const;

        static eka2l1::rect rect_of(const layout_kind kind);

        /**
         * \brief Find the frontmost window group of the screen that has a pane layout.
         */
        window_group *find_anchor(screen *scr, layout_kind &kind) const;

        void draw(drivers::graphics_command_builder &builder, screen *scr, const layout_kind kind,
            const common::region &visible);
    };
}
