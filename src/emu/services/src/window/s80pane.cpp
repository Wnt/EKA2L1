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

#include <services/window/s80pane.h>
#include <services/window/screen.h>
#include <services/window/window.h>
#include <services/window/classes/wingroup.h>

#include <services/fbs/bitmap.h>
#include <services/fbs/fbs.h>

#include <common/buffer.h>
#include <common/cvt.h>
#include <common/log.h>
#include <drivers/graphics/graphics.h>
#include <drivers/itc.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <loader/mbm.h>
#include <system/epoc.h>
#include <vfs/vfs.h>

namespace eka2l1::epoc {
    // BASESKIN.RSG (Series 80 DP 2.0 SDK): the layouts an application passes to SetStatusPaneLayout.
    static constexpr std::uint32_t R_BSKN_INDICATOR_LAYOUT = 0xD118004;
    static constexpr std::uint32_t R_BSKN_INDICATOR_LAYOUT_WIDE = 0xD118006;

    // Skin bitmaps (ROM: Z:\System\Data\skinerin). The pane's background is the application view
    // background at the pane's own screen position: #2 is the Desk-style one (92-px left column with the
    // icon swoosh), #0 the narrow-strip one. SKINSTATUSPANE.MBM #17 = qgn_bstat_networkunavailable; #7 is
    // the battery bitmap the ROM server draws with no charger/battery driver (matched pixel for pixel).
    static const char16_t *SKIN_APPVIEW = u"Z:\\System\\Data\\skinerin\\skinappview.mbm";
    static const char16_t *SKIN_STATUSPANE = u"Z:\\System\\Data\\skinerin\\skinstatuspane.mbm";
    static constexpr int SKIN_APPVIEW_NARROW = 0;
    static constexpr int SKIN_APPVIEW_WIDE = 2;
    static constexpr int SKIN_NETWORK_UNAVAILABLE = 17;
    static constexpr int SKIN_BATTERY = 7;

    s80_status_pane::s80_status_pane(window_server *serv)
        : serv_(serv) {
    }

    void s80_status_pane::set_layout(kernel::process *pr, const std::uint32_t layout_res) {
        if (!pr) {
            return;
        }

        layouts_[pr->unique_id()] = layout_res;
    }

    s80_status_pane::layout_kind s80_status_pane::layout_of(window_group *group) const {
        if (!group || !group->client) {
            return layout_none;
        }

        kernel::thread *owner = group->client->get_client();
        if (!owner || !owner->owning_process()) {
            return layout_none;
        }

        auto ite = layouts_.find(owner->owning_process()->unique_id());
        if (ite == layouts_.end()) {
            return layout_none;
        }

        switch (ite->second) {
        case R_BSKN_INDICATOR_LAYOUT:
            return layout_narrow;

        case R_BSKN_INDICATOR_LAYOUT_WIDE:
            return layout_wide;

        default:
            break;
        }

        return layout_none;
    }

    eka2l1::rect s80_status_pane::rect_of(const layout_kind kind) {
        switch (kind) {
        case layout_narrow:
            return eka2l1::rect({ 0, 0 }, { 32, 200 });

        case layout_wide:
            return eka2l1::rect({ 0, 100 }, { 92, 100 });

        default:
            break;
        }

        return eka2l1::rect({ 0, 0 }, { 0, 0 });
    }

    window_group *s80_status_pane::find_anchor(screen *scr, layout_kind &kind) const {
        kind = layout_none;

        if (!scr || !scr->root) {
            return nullptr;
        }

        for (epoc::window *win = scr->root->child; win; win = win->sibling) {
            if (win->type != epoc::window_kind::group) {
                continue;
            }

            window_group *group = reinterpret_cast<window_group *>(win);
            const layout_kind k = layout_of(group);

            if (k != layout_none) {
                kind = k;
                return group;
            }
        }

        return nullptr;
    }

    bool s80_status_pane::load_image(drivers::graphics_driver *driver, image &img, const std::u16string &path,
        const int index, const bool key_magenta) {
        if (img.handle_) {
            return true;
        }

        if (img.tried_) {
            return false;
        }

        img.tried_ = true;

        io_system *io = serv_->get_system()->get_io_system();
        symfile f = io ? io->open_file(path, READ_MODE | BIN_MODE) : nullptr;

        if (!f) {
            LOG_WARN(SERVICE_WINDOW, "Series 80 status pane: cannot open {}", common::ucs2_to_utf8(path));
            return false;
        }

        ro_file_stream stream(f.get());
        loader::mbm_file parser(reinterpret_cast<common::ro_stream *>(&stream));

        if (!parser.do_read_headers() || (index >= static_cast<int>(parser.sbm_headers.size()))) {
            LOG_WARN(SERVICE_WINDOW, "Series 80 status pane: no bitmap {} in {}", index, common::ucs2_to_utf8(path));
            return false;
        }

        const eka2l1::vec2 size = parser.sbm_headers[index].size_pixels;
        if ((size.x <= 0) || (size.y <= 0)) {
            return false;
        }

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.x) * size.y * 4);
        common::wo_buf_stream dest(pixels.data(), pixels.size());

        if (!convert_to_rgba8888(serv_->get_fbs_server(), parser, index, dest)) {
            LOG_WARN(SERVICE_WINDOW, "Series 80 status pane: bitmap {} of {} does not decode", index, common::ucs2_to_utf8(path));
            return false;
        }

        // The driver's 32-bit bitmaps are BGRA. The indicators have no masks: their background is the
        // skin's magenta key colour.
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            const std::uint8_t r = pixels[i];
            const std::uint8_t g = pixels[i + 1];
            const std::uint8_t b = pixels[i + 2];

            pixels[i] = b;
            pixels[i + 2] = r;
            pixels[i + 3] = (key_magenta && (r >= 0xF8) && (g <= 0x04) && (b >= 0xF8)) ? 0 : 0xFF;
        }

        img.handle_ = drivers::create_bitmap(driver, size, 32);
        if (!img.handle_) {
            return false;
        }

        drivers::graphics_command_builder builder;
        builder.update_bitmap(img.handle_, reinterpret_cast<const char *>(pixels.data()), pixels.size(), { 0, 0 }, size);

        drivers::command_list list = builder.retrieve_command_list();
        driver->submit_command_list(list);

        img.size_ = size;
        return true;
    }

    void s80_status_pane::draw(drivers::graphics_command_builder &builder, screen *scr, const layout_kind kind,
        const common::region &visible) {
        if ((kind == layout_none) || visible.empty()) {
            return;
        }

        drivers::graphics_driver *driver = serv_->get_graphics_driver();
        if (!driver) {
            return;
        }

        image &background = (kind == layout_wide) ? background_wide_ : background_narrow_;
        const int background_index = (kind == layout_wide) ? SKIN_APPVIEW_WIDE : SKIN_APPVIEW_NARROW;

        const bool have_background = load_image(driver, background, SKIN_APPVIEW, background_index, false);
        const bool have_network = load_image(driver, network_, SKIN_STATUSPANE, SKIN_NETWORK_UNAVAILABLE, true);
        const bool have_battery = load_image(driver, battery_, SKIN_STATUSPANE, SKIN_BATTERY, true);

        const float scale = scr->display_scale_factor;
        const eka2l1::rect pane = rect_of(kind);

        builder.clip_bitmap_region(visible, scale);
        builder.set_feature(drivers::graphics_feature::blend, false);

        if (have_background) {
            // The background bitmap is screen-sized: the pane shows the part under it.
            builder.draw_bitmap(background.handle_, 0, eka2l1::rect(pane.top * scale, pane.size * scale), pane);
        } else {
            // Without the skin at least keep what is behind from showing through.
            builder.set_brush_color_detail(eka2l1::vec4(155, 153, 172, 255));
            builder.draw_rectangle(eka2l1::rect(pane.top * scale, pane.size * scale));
        }

        builder.set_feature(drivers::graphics_feature::blend, true);

        // Indicator positions, from the ROM server's frames (screen coordinates at 1x).
        const eka2l1::vec2 network_pos = (kind == layout_wide) ? eka2l1::vec2(4, 150) : eka2l1::vec2(5, 102);
        const eka2l1::vec2 battery_pos = (kind == layout_wide) ? eka2l1::vec2(63, 151) : eka2l1::vec2(5, 149);

        if (have_network) {
            builder.draw_bitmap(network_.handle_, 0, eka2l1::rect(network_pos * scale, network_.size_ * scale),
                eka2l1::rect({ 0, 0 }, network_.size_));
        }

        if (have_battery) {
            builder.draw_bitmap(battery_.handle_, 0, eka2l1::rect(battery_pos * scale, battery_.size_ * scale),
                eka2l1::rect({ 0, 0 }, battery_.size_));
        }

        builder.set_feature(drivers::graphics_feature::blend, false);
        builder.set_feature(drivers::graphics_feature::clipping, false);
        builder.set_feature(drivers::graphics_feature::stencil_test, false);
    }
}
