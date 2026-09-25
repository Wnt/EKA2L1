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
        // The pane lives as long as the window server, which the kernel wipes out after the font and bitmap
        // server: by now clock_font_ points into FBS's freed object container, and deref() on it called through a
        // dead vtable (host SIGSEGV on every quit after the pane was drawn). The reference goes with FBS.
        clock_font_ = nullptr;
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

    // Decode a skin bitmap into a driver bitmap (BGRA; the skin's magenta key colour made transparent on request).
    static bool load_skin_bitmap(window_server *serv, drivers::graphics_driver *driver, drivers::handle &handle,
        eka2l1::vec2 &out_size, const std::u16string &path, const int index, const bool key_magenta) {
        io_system *io = serv->get_system()->get_io_system();
        symfile f = io ? io->open_file(path, READ_MODE | BIN_MODE) : nullptr;

        if (!f) {
            LOG_WARN(SERVICE_WINDOW, "Series 80 skin: cannot open {}", common::ucs2_to_utf8(path));
            return false;
        }

        ro_file_stream stream(f.get());
        loader::mbm_file parser(reinterpret_cast<common::ro_stream *>(&stream));

        if (!parser.do_read_headers() || (index >= static_cast<int>(parser.sbm_headers.size()))) {
            LOG_WARN(SERVICE_WINDOW, "Series 80 skin: no bitmap {} in {}", index, common::ucs2_to_utf8(path));
            return false;
        }

        const eka2l1::vec2 size = parser.sbm_headers[index].size_pixels;
        if ((size.x <= 0) || (size.y <= 0)) {
            return false;
        }

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.x) * size.y * 4);
        common::wo_buf_stream dest(pixels.data(), pixels.size());

        if (!convert_to_rgba8888(serv->get_fbs_server(), parser, index, dest)) {
            LOG_WARN(SERVICE_WINDOW, "Series 80 skin: bitmap {} of {} does not decode", index, common::ucs2_to_utf8(path));
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

        handle = drivers::create_bitmap(driver, size, 32);
        if (!handle) {
            return false;
        }

        drivers::graphics_command_builder builder;
        builder.update_bitmap(handle, reinterpret_cast<const char *>(pixels.data()), pixels.size(), { 0, 0 }, size);

        drivers::command_list list = builder.retrieve_command_list();
        driver->submit_command_list(list);

        out_size = size;
        return true;
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
        return load_skin_bitmap(serv_, driver, img.handle_, img.size_, path, index, key_magenta);
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

namespace eka2l1::epoc {
    // The ROM Eikon server's note (EKA2L1_ROM_EIKSRV=1, "Program closed"), measured at 1x: centred on the
    // screen right of the narrow status strip (32..555), a 3-pixel frame, a 27-pixel skinned title bar.
    static const char16_t *SKIN_DIALOG_FRAME = u"Z:\\System\\Data\\skinerin\\skindialogframe.mbm";
    static constexpr int SKIN_DIALOG_TITLE = 8;

    static constexpr std::int32_t NOTE_AREA_LEFT = 32;
    static constexpr std::int32_t NOTE_AREA_RIGHT = 555;
    static constexpr std::int32_t NOTE_WIDTH = 300;
    static constexpr std::int32_t NOTE_FRAME = 3;
    static constexpr std::int32_t NOTE_TITLE_HEIGHT = 27;
    static constexpr std::int32_t NOTE_LINE_HEIGHT = 20;
    static constexpr std::int32_t NOTE_TEXT_MARGIN = 10;
    static constexpr std::int32_t NOTE_MAX_LINES = 4;

    // The command button array right of the application area: four 50-pixel slots.
    static constexpr std::int32_t CBA_LEFT = 555;
    static constexpr std::int32_t CBA_WIDTH = 85;
    static constexpr std::int32_t CBA_TOP_BAND = 6;
    static constexpr std::int32_t CBA_SLOT_HEIGHT = 50;
    static constexpr std::int32_t CBA_TEXT_RIGHT_MARGIN = 4;

    // Host key codes the frontends deliver (drivers input events carry Qt's key numbers).
    static constexpr std::uint32_t NOTE_HOST_KEY_ESCAPE = 0x01000000;
    static constexpr std::uint32_t NOTE_HOST_KEY_RETURN = 0x01000004;
    static constexpr std::uint32_t NOTE_HOST_KEY_ENTER = 0x01000005;
    static constexpr std::uint32_t NOTE_HOST_KEY_F1 = 0x01000030;

    s80_note::s80_note(window_server *serv)
        : serv_(serv) {
    }

    eka2l1::rect s80_note::rect() {
        return eka2l1::rect({ NOTE_AREA_LEFT + (NOTE_AREA_RIGHT - NOTE_AREA_LEFT - NOTE_WIDTH) / 2, 0 }, { NOTE_WIDTH, 0 });
    }

    bool s80_note::show(const std::u16string &title, const std::u16string &text, const std::u16string &button1,
        const std::u16string &button2, completion done) {
        {
            const std::lock_guard<std::mutex> guard(lock_);
            if (active_) {
                return false;
            }

            active_ = true;
            title_ = title;
            text_ = text;

            split_title(title_, text_);
            button1_ = button1;
            button2_ = button2;
            done_ = std::move(done);
        }

        LOG_INFO(SERVICE_WINDOW, "Series 80 note up: \"{}\" / \"{}\" [{}|{}]", common::ucs2_to_utf8(title),
            common::ucs2_to_utf8(text), common::ucs2_to_utf8(button1), common::ucs2_to_utf8(button2));

        schedule_redraw();
        return true;
    }

    void s80_note::split_title(std::u16string &title, std::u16string &text) {
        // CEikonEnv's error alert packs "Title:\nMessage\n" into the first line and leaves the second
        // empty: the title bar takes the first line, the body the rest.
        const std::size_t title_end = title.find_first_of(u"\n\u2029");
        if (title_end != std::u16string::npos) {
            std::u16string rest = title.substr(title_end + 1);
            title.resize(title_end);

            if (!text.empty()) {
                if (!rest.empty() && (rest.back() != u'\n')) {
                    rest += u'\n';
                }
                rest += text;
            }

            text = rest;
        }

        while (!text.empty() && ((text.back() == u'\n') || (text.back() == u' '))) {
            text.pop_back();
        }
    }

    bool s80_note::active() {
        const std::lock_guard<std::mutex> guard(lock_);
        return active_;
    }

    void s80_note::answer(const int button) {
        completion done;
        {
            const std::lock_guard<std::mutex> guard(lock_);
            if (!active_) {
                return;
            }

            active_ = false;
            done = std::move(done_);
            done_ = nullptr;
        }

        LOG_INFO(SERVICE_WINDOW, "Series 80 note answered with button {}", button);

        // The completion takes the kernel lock itself (it completes the notifier's request).
        if (done) {
            done(button);
        }

        if (serv_) {
            kernel_system *kern = serv_->get_kernel_system();
            kern->lock();
            schedule_redraw();
            kern->unlock();
        }
    }

    bool s80_note::handle_key_press(const std::uint32_t host_key) {
        bool has_second = false;
        {
            const std::lock_guard<std::mutex> guard(lock_);
            if (!active_) {
                return false;
            }

            has_second = !button2_.empty();
        }

        switch (host_key) {
        case NOTE_HOST_KEY_RETURN:
        case NOTE_HOST_KEY_ENTER:
        case NOTE_HOST_KEY_F1:
        case NOTE_HOST_KEY_F1 + 12: // Joystick centre
            answer(0);
            break;

        case NOTE_HOST_KEY_ESCAPE:
            // Esc takes the second (cancelling) button when there is one.
            answer(has_second ? 1 : 0);
            break;

        case NOTE_HOST_KEY_F1 + 3:
            if (has_second) {
                answer(1);
            }
            break;

        default:
            break;
        }

        // Every other key press stays with the note, as a modal dialog keeps the focus on the device.
        return true;
    }

    // Called with the kernel lock held.
    void s80_note::schedule_redraw() {
        if (!serv_) {
            return;
        }

        drivers::graphics_driver *driver = serv_->get_graphics_driver();

        for (epoc::screen *scr = serv_->get_screens(); scr; scr = scr->next) {
            scr->note = this;
            scr->flags_ |= epoc::screen::FLAG_SERVER_REDRAW_PENDING;

            if (driver) {
                serv_->get_anim_scheduler()->schedule_if_sooner(driver, scr, serv_->get_ntimer()->microseconds());
            }
        }
    }

    // The 9300 GDR's faces: "System" is the bold family (every size carries the bold attribute), "SystemLight"
    // the regular one. The window server makes no fonts: it takes one of those the applications hold.
    fbsfont *s80_note::find_font(const bool bold, const std::int32_t design_height) {
        fbs_server *fbss = serv_->get_fbs_server();
        if (!fbss) {
            return nullptr;
        }

        const std::u16string family = bold ? u"System" : u"SystemLight";
        fbsfont *best = nullptr;
        std::int32_t best_delta = 0x7FFFFFFF;

        for (fbsfont *font : fbss->live_fonts()) {
            if (font->of_info.family != family) {
                continue;
            }

            std::int32_t delta = std::abs(font->of_info.metrics.design_height - design_height) * 2;
            if (bold && (font->of_info.metric_identifier != CLOCK_FONT_METRIC)) {
                // Prefer the face the command buttons and the pane clock use.
                delta++;
            }

            if (delta < best_delta) {
                best = font;
                best_delta = delta;
            }
        }

        return best;
    }

    void s80_note::draw_text(drivers::graphics_command_builder &builder, screen *scr, fbsfont *font, const std::u16string &text,
        const eka2l1::rect &box, const std::uint32_t alignment, const eka2l1::vec4 &colour) {
        if (!font || text.empty()) {
            return;
        }

        gdi_store_command command;
        command.opcode_ = gdi_store_command_draw_text;

        gdi_store_command_draw_text_data &data = command.get_data_struct<gdi_store_command_draw_text_data>();
        data.string_ = reinterpret_cast<char16_t *>(command.allocate_dynamic_data((text.length() + 1) * sizeof(char16_t)));
        std::memcpy(data.string_, text.c_str(), (text.length() + 1) * sizeof(char16_t));

        data.alignment_ = alignment;
        data.text_box_ = box;
        data.fbs_font_ptr_ = font;
        data.color_ = colour;

        gdi_store_command_segment segment;
        segment.add_command(command);

        common::region clip;
        clip.add_rect(eka2l1::rect({ 0, 0 }, scr->current_mode().size));

        const drivers::filter_option filter = (serv_->get_kernel_system()->get_config()->nearest_neighbor_filtering
            ? drivers::filter_option::nearest : drivers::filter_option::linear);

        builder.set_feature(drivers::graphics_feature::blend, false);
        builder.clip_bitmap_region(clip, scr->display_scale_factor);

        gdi_command_builder gdi_builder(serv_->get_graphics_driver(), builder, *serv_->get_bitmap_cache(), filter,
            eka2l1::vec2(0, 0), scr->display_scale_factor, clip);
        gdi_builder.build_segment(segment);
    }

    // Break the text into lines that fit the width, at spaces (and at the text's own line breaks).
    static std::vector<std::u16string> wrap_note_text(fbsfont *font, const std::u16string &text, const std::int32_t width) {
        std::vector<std::u16string> lines;
        if (!font) {
            lines.push_back(text);
            return lines;
        }

        auto advance_of = [font](const char16_t c) -> std::int32_t {
            return static_cast<std::int32_t>(font->of_info.adapter->get_glyph_advance(font->of_info.idx, c,
                font->of_info.metric_identifier));
        };

        std::u16string line;
        std::int32_t line_width = 0;
        std::u16string word;
        std::int32_t word_width = 0;

        auto flush_word = [&]() {
            if (word.empty()) {
                return;
            }

            if (!line.empty() && (line_width + word_width > width)) {
                while (!line.empty() && (line.back() == u' ')) {
                    line.pop_back();
                }
                lines.push_back(line);
                line.clear();
                line_width = 0;
            }

            line += word;
            line_width += word_width;
            word.clear();
            word_width = 0;
        };

        for (const char16_t c : text) {
            if ((c == u'\n') || (c == u'\r') || (c == 0x2029)) {
                flush_word();
                if (!line.empty()) {
                    lines.push_back(line);
                }
                line.clear();
                line_width = 0;
                continue;
            }

            word += c;
            word_width += advance_of(c);

            if (c == u' ') {
                flush_word();
            }
        }

        flush_word();
        if (!line.empty()) {
            lines.push_back(line);
        }

        return lines;
    }

    void s80_note::draw(drivers::graphics_command_builder &builder, screen *scr) {
        std::u16string title;
        std::u16string text;
        std::u16string button1;
        std::u16string button2;
        {
            const std::lock_guard<std::mutex> guard(lock_);
            if (!active_) {
                return;
            }

            title = title_;
            text = text_;
            button1 = button1_;
            button2 = button2_;
        }

        drivers::graphics_driver *driver = serv_->get_graphics_driver();
        if (!driver) {
            return;
        }

        // A note with no labels still needs a way out: the device's alerts fall back to one OK button.
        if (button1.empty() && button2.empty()) {
            button1 = u"OK";
        }

        // Titles such as "System:" come with the colon the two-line alert puts after them.
        while (!title.empty() && ((title.back() == u':') || (title.back() == u' '))) {
            title.pop_back();
        }

        fbsfont *text_font = find_font(false, 20);
        fbsfont *cba_font = find_font(true, 20);

        if (!fonts_logged_) {
            fonts_logged_ = true;
            fbs_server *fbss = serv_->get_fbs_server();
            if (fbss) {
                for (fbsfont *font : fbss->live_fonts()) {
                    LOG_INFO(SERVICE_WINDOW, "Series 80 note: live font {} '{}' style 0x{:X} design height {} metric {}", font->id,
                        common::ucs2_to_utf8(font->of_info.family), font->of_info.face_attrib.style,
                        font->of_info.metrics.design_height, font->of_info.metric_identifier);
                }
            }
            LOG_INFO(SERVICE_WINDOW, "Series 80 note: text font {}, command button font {}", text_font ? text_font->id : 0,
                cba_font ? cba_font->id : 0);
        }

        std::vector<std::u16string> lines = wrap_note_text(text_font, text, NOTE_WIDTH - 2 * (NOTE_FRAME + NOTE_TEXT_MARGIN));
        if (lines.size() > NOTE_MAX_LINES) {
            lines.resize(NOTE_MAX_LINES);
        }

        const std::int32_t line_count = std::max<std::int32_t>(1, static_cast<std::int32_t>(lines.size()));
        const std::int32_t height = 2 * NOTE_FRAME + NOTE_TITLE_HEIGHT + line_count * NOTE_LINE_HEIGHT + 2 * NOTE_TEXT_MARGIN;
        const eka2l1::vec2 screen_size = scr->current_mode().size;

        eka2l1::rect box = rect();
        box.top.y = (screen_size.y - height) / 2;
        box.size.y = height;

        const float scale = scr->display_scale_factor;

        builder.set_feature(drivers::graphics_feature::clipping, false);
        builder.set_feature(drivers::graphics_feature::stencil_test, false);
        builder.set_feature(drivers::graphics_feature::blend, false);

        auto fill = [&](const eka2l1::rect &r, const eka2l1::vec4 &colour) {
            builder.set_brush_color_detail(colour);
            builder.draw_rectangle(eka2l1::rect(r.top * scale, r.size * scale));
        };

        // Frame: three one-pixel rings, outermost darkest.
        static const eka2l1::vec4 ring_colours[NOTE_FRAME] = {
            { 66, 65, 74, 255 }, { 140, 138, 156, 255 }, { 214, 211, 222, 255 }
        };

        for (std::int32_t i = 0; i < NOTE_FRAME; i++) {
            fill(eka2l1::rect(box.top + eka2l1::vec2(i, i), box.size - eka2l1::vec2(2 * i, 2 * i)), ring_colours[i]);
        }

        const eka2l1::rect inner(box.top + eka2l1::vec2(NOTE_FRAME, NOTE_FRAME), box.size - eka2l1::vec2(2 * NOTE_FRAME, 2 * NOTE_FRAME));
        const eka2l1::rect title_rect(inner.top, { inner.size.x, NOTE_TITLE_HEIGHT });
        const eka2l1::rect body_rect(inner.top + eka2l1::vec2(0, NOTE_TITLE_HEIGHT), inner.size - eka2l1::vec2(0, NOTE_TITLE_HEIGHT));

        if (!title_bg_tried_) {
            title_bg_tried_ = true;
            load_skin_bitmap(serv_, driver, title_bg_, title_bg_size_, SKIN_DIALOG_FRAME, SKIN_DIALOG_TITLE, false);
        }

        if (title_bg_) {
            builder.draw_bitmap(title_bg_, 0, eka2l1::rect(title_rect.top * scale, title_rect.size * scale),
                eka2l1::rect({ 0, 0 }, { std::min(title_rect.size.x, title_bg_size_.x), std::min(title_rect.size.y, title_bg_size_.y) }));
        } else {
            fill(title_rect, { 214, 211, 222, 255 });
        }

        fill(body_rect, { 255, 255, 255, 255 });

        const eka2l1::vec4 black(0, 0, 0, 255);

        // The text box's top is the baseline.
        draw_text(builder, scr, text_font, title, eka2l1::rect(title_rect.top + eka2l1::vec2(NOTE_TEXT_MARGIN, NOTE_TITLE_HEIGHT - 7),
            { title_rect.size.x - 2 * NOTE_TEXT_MARGIN, NOTE_TITLE_HEIGHT }), static_cast<std::uint32_t>(epoc::text_alignment::left), black);

        for (std::size_t i = 0; i < lines.size(); i++) {
            const std::int32_t baseline = body_rect.top.y + NOTE_TEXT_MARGIN + static_cast<std::int32_t>(i + 1) * NOTE_LINE_HEIGHT - 5;
            draw_text(builder, scr, text_font, lines[i], eka2l1::rect({ body_rect.top.x + NOTE_TEXT_MARGIN, baseline },
                { body_rect.size.x - 2 * NOTE_TEXT_MARGIN, NOTE_LINE_HEIGHT }), static_cast<std::uint32_t>(epoc::text_alignment::left), black);
        }

        // The command buttons belong to the note while it is up: the first label in slot 1, the second in slot 4.
        fill(eka2l1::rect({ CBA_LEFT, CBA_TOP_BAND }, { CBA_WIDTH, screen_size.y - CBA_TOP_BAND }), { 181, 178, 189, 255 });

        const std::u16string labels[4] = { button1, u"", u"", button2 };
        for (int slot = 0; slot < 4; slot++) {
            if (labels[slot].empty()) {
                continue;
            }

            const std::int32_t baseline = slot * CBA_SLOT_HEIGHT + 31;
            draw_text(builder, scr, cba_font, labels[slot], eka2l1::rect({ CBA_LEFT, baseline },
                { CBA_WIDTH - CBA_TEXT_RIGHT_MARGIN, CBA_SLOT_HEIGHT }), static_cast<std::uint32_t>(epoc::text_alignment::right), black);
        }

        builder.set_feature(drivers::graphics_feature::blend, false);
        builder.set_feature(drivers::graphics_feature::clipping, false);
        builder.set_feature(drivers::graphics_feature::stencil_test, false);
    }
}
