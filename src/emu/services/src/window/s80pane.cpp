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
#include <services/window/classes/gstore.h>
#include <services/window/classes/plugins/anim/clock/clock.h>

#include <services/fbs/bitmap.h>
#include <services/fbs/fbs.h>
#include <services/fbs/font.h>

#include <common/buffer.h>
#include <common/cvt.h>
#include <common/log.h>
#include <drivers/graphics/graphics.h>
#include <drivers/itc.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/property.h>
#include <utils/locale.h>
#include <utils/system.h>
#include <config/config.h>
#include <loader/mbm.h>
#include <system/epoc.h>
#include <vfs/vfs.h>

#include <cstdlib>
#include <cstring>

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

    // The wide pane's clock, as the ROM server sets up its RDigitalClock (anim trace of the ROM run): at
    // (16,38) 60x26 in the pane window, white, sections "%-B" left / "%J%:1%T" centred / "%+B" right,
    // bottom-aligned without descent, 2 px from the bottom. The face is the 9300 GDR's System bold of
    // design height 20 whose metric index is 3: of the System bold faces the applications hold, the only
    // one that reproduces the ROM server's frame pixel for pixel (raced against metrics 1, 8 and 13).
    static const eka2l1::rect CLOCK_RECT({ 16, 138 }, { 60, 26 });
    static constexpr std::uint32_t CLOCK_FONT_METRIC = 3;
    static constexpr std::int32_t CLOCK_FONT_DESIGN_HEIGHT = 20;
    static constexpr std::int32_t CLOCK_BOTTOM_MARGIN = 2;
    static constexpr std::int64_t SECONDS_FROM_0AD_TO_1970 = 62168256000LL;

    s80_status_pane::s80_status_pane(window_server *serv)
        : serv_(serv) {
    }

    s80_status_pane::~s80_status_pane() {
        if (clock_font_) {
            clock_font_->deref();
        }
    }

    // Local time as the ROM server's clock gets it: universal time plus the guest locale's offset
    // (TLocale::UniversalTimeOffset on EKA1: the zone's offset, one hour more while the home zone keeps
    // daylight saving), which is what it passes to the clock anim with SetUniversalTimeOffset.
    std::int64_t s80_status_pane::local_seconds() const {
        kernel_system *kern = serv_->get_kernel_system();
        std::int64_t seconds = static_cast<std::int64_t>(kern->universal_time() / 1000000ULL) - SECONDS_FROM_0AD_TO_1970;

        property_ptr prop = kern->get_prop(epoc::SYS_CATEGORY, epoc::LOCALE_DATA_KEY);
        std::optional<epoc::locale> loc = prop ? prop->get_pkg<epoc::locale>() : std::nullopt;

        if (loc) {
            seconds += epoc::locale_effective_utc_offset(*loc);
        } else {
            seconds += kern->utc_offset();
        }

        return seconds;
    }

    bool s80_status_pane::clock_changed(const layout_kind kind) {
        if (kind != layout_wide) {
            return false;
        }

        return (local_seconds() / 60) != shown_minute_;
    }

    fbsfont *s80_status_pane::clock_font() {
        if (clock_font_) {
            return clock_font_;
        }

        fbs_server *fbss = serv_->get_fbs_server();
        if (!fbss) {
            return nullptr;
        }

        // The window server makes no fonts of its own: take the ROM server clock's face from those the
        // applications hold (Series 80 controls use System bold in several sizes). Early in boot it may not
        // exist yet: draw with the closest size meanwhile and keep looking.
        fbsfont *best = nullptr;
        std::int32_t best_delta = 0x7FFFFFFF;

        for (fbsfont *font : fbss->live_fonts()) {
            if ((font->of_info.family != u"System") || !(font->of_info.face_attrib.style & epoc::open_font_face_attrib::bold)) {
                continue;
            }

            if (font->of_info.metric_identifier == CLOCK_FONT_METRIC) {
                font->ref();
                clock_font_ = font;

                LOG_INFO(SERVICE_WINDOW, "Series 80 status pane clock font: FBS font {}", font->id);
                return clock_font_;
            }

            const std::int32_t delta = std::abs(font->of_info.metrics.design_height - CLOCK_FONT_DESIGN_HEIGHT);
            if (delta < best_delta) {
                best = font;
                best_delta = delta;
            }
        }

        return best;
    }

    void s80_status_pane::draw_clock(drivers::graphics_command_builder &builder, screen *scr, const common::region &visible) {
        fbsfont *font = clock_font();
        if (!font) {
            return;
        }

        const std::int64_t now = local_seconds();
        shown_minute_ = now / 60;

        struct section_def {
            const char16_t *format;
            epoc::text_alignment alignment;
        };

        static const section_def sections[] = {
            { u"%-B", epoc::text_alignment::left },
            { u"%J%:1%T", epoc::text_alignment::center },
            { u"%+B", epoc::text_alignment::right }
        };

        const std::int32_t baseline = CLOCK_RECT.top.y + CLOCK_RECT.size.y - CLOCK_BOTTOM_MARGIN;
        gdi_store_command_segment segment;

        for (const section_def &section : sections) {
            const std::u16string text = clock_format_time(section.format, now);
            if (text.empty()) {
                continue;
            }

            gdi_store_command command;
            command.opcode_ = gdi_store_command_draw_text;

            gdi_store_command_draw_text_data &data = command.get_data_struct<gdi_store_command_draw_text_data>();
            data.string_ = reinterpret_cast<char16_t *>(command.allocate_dynamic_data((text.length() + 1) * sizeof(char16_t)));
            std::memcpy(data.string_, text.c_str(), (text.length() + 1) * sizeof(char16_t));

            data.alignment_ = static_cast<std::uint32_t>(section.alignment);
            data.text_box_ = eka2l1::rect(eka2l1::vec2(CLOCK_RECT.top.x, baseline), CLOCK_RECT.size);
            data.fbs_font_ptr_ = font;
            data.color_ = eka2l1::vec4(255, 255, 255, 255);

            segment.add_command(command);
        }

        if (segment.commands_.empty()) {
            return;
        }

        common::region clip;
        clip.add_rect(CLOCK_RECT);
        clip = clip.intersect(visible);

        if (clip.empty()) {
            return;
        }

        const drivers::filter_option filter = (serv_->get_kernel_system()->get_config()->nearest_neighbor_filtering
            ? drivers::filter_option::nearest : drivers::filter_option::linear);

        builder.set_feature(drivers::graphics_feature::blend, false);
        builder.clip_bitmap_region(clip, scr->display_scale_factor);

        gdi_command_builder gdi_builder(serv_->get_graphics_driver(), builder, *serv_->get_bitmap_cache(), filter,
            eka2l1::vec2(0, 0), scr->display_scale_factor, clip);
        gdi_builder.build_segment(segment);
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

        if (kind == layout_wide) {
            draw_clock(builder, scr, visible);
        }

        builder.set_feature(drivers::graphics_feature::blend, false);
        builder.set_feature(drivers::graphics_feature::clipping, false);
        builder.set_feature(drivers::graphics_feature::stencil_test, false);
    }
}
