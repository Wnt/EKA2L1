/*
 * Copyright (c) 2023 EKA2L1 Team
 *
 * This file is part of EKA2L1 project
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

#include <services/window/classes/plugins/animdll.h>

#include <common/rgb.h>
#include <common/vecx.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eka2l1 {
    struct fbsbitmap;
    struct fbsfont;
    class kernel_system;
    class window_server;
}

namespace eka2l1::epoc {
    struct gdi_store_command_segment;

    // TTimeDeviceCommand of the clock anim DLL (CLOCKA.DLL, CL_STD.H). The class of a command is in bits 12-15:
    // a display command (0x2000 set) goes to the display, whatever its type.
    enum clock_anim_command : std::int32_t {
        clock_command = 0x1000,
        display_command = 0x2000,
        digital_display_command = 0x4000 | display_command,
        analog_display_command = 0x8000 | display_command,
        clock_command_set_universal_time_offset = clock_command | 1,
        display_command_set_visible = display_command | 1,
        display_command_set_position_and_size = display_command | 2,
        display_command_set_position = display_command | 3,
        display_command_set_size = display_command | 4,
        display_command_update_display = display_command | 5,
        display_command_draw = display_command | 6,
        digital_display_command_set_background_color = digital_display_command | 1,
        digital_display_command_set_text_color = digital_display_command | 2,
        analog_display_command_set_pen_color = analog_display_command | 1,
        analog_display_command_set_brush_color = analog_display_command | 2,

        // Series 80 v2 only, numbered from what the 9300 sends (its CL_STD.H is not public): the status pane's
        // clock (CEikCBAResourceConstructedClock) sends it once after construction with one TInt, 0, which fits
        // CEikLocaleConformantClock::SetDateTimeToggle(aTextSection): the section a later ToggleDateTime shows the
        // date in.
        digital_display_command_set_date_time_toggle_s80 = digital_display_command | 5
    };

    enum clock_display_type : std::int32_t {
        clock_display_digital = 0,
        clock_display_analog = 1
    };

    enum clock_hand_type : std::int32_t {
        clock_hand_one_rev_per_12_hours = 0,
        clock_hand_one_rev_per_hour = 1,
        clock_hand_one_rev_per_minute = 2
    };

    enum clock_hand_feature_type : std::int32_t {
        clock_hand_feature_line = 0,
        clock_hand_feature_polyline = 1,
        clock_hand_feature_circle = 2
    };

    struct clock_shadow {
        bool on_ = false;
        common::rgba color_ = 0;
        eka2l1::vec2 offset_{ 0, 0 };
    };

    struct clock_margins {
        std::int32_t left_ = 0;
        std::int32_t right_ = 0;
        std::int32_t top_ = 0;
        std::int32_t bottom_ = 0;
    };

    struct clock_text_section {
        std::uint32_t font_handle_ = 0;
        fbsfont *font_ = nullptr;
        common::rgba color_ = 0;
        std::int32_t horizontal_alignment_ = 0;
        std::int32_t vertical_alignment_ = 0;
        std::int32_t horizontal_margin_ = 0;
        std::int32_t vertical_margin_ = 0;
        std::u16string format_;
    };

    struct clock_hand_feature {
        clock_hand_feature_type type_ = clock_hand_feature_line;
        std::int32_t pen_style_ = 0;
        common::rgba pen_color_ = 0;
        eka2l1::vec2 pen_size_{ 1, 1 };
        std::int32_t brush_style_ = 0;
        common::rgba brush_color_ = 0;
        bool closed_ = false;
        std::vector<eka2l1::vec2> points_; ///< Line: start and end. Polyline: every point. Circle: the centre.
        std::int32_t radius_ = 0;
    };

    struct clock_hand {
        clock_hand_type type_ = clock_hand_one_rev_per_12_hours;
        std::vector<clock_hand_feature> features_;
    };

    /**
     * \brief Everything a clock's constructor buffer carries (RTimeDevice::AppendXxxConstructorArgsL).
     *
     * Colours are the raw TRgb values and the fonts and bitmaps their handles, as the client sent them.
     */
    struct clock_constructor_args {
        std::int32_t universal_time_offset_ = 0;
        clock_display_type type_ = clock_display_digital;

        eka2l1::vec2 position_{ 0, 0 };
        eka2l1::vec2 size_{ 0, 0 };
        clock_margins margins_;
        clock_shadow shadow_;

        common::rgba background_color_ = 0;
        std::vector<clock_text_section> sections_;

        std::uint32_t face_handle_ = 0;
        std::uint32_t face_mask_handle_ = 0;
        eka2l1::vec2 hand_centre_offset_{ 0, 0 };
        std::vector<clock_hand> hands_;

        bool has_am_pm_ = false;
        eka2l1::vec2 am_pm_position_{ 0, 0 };
        eka2l1::vec2 am_pm_size_{ 0, 0 };
        clock_shadow am_pm_shadow_;
        common::rgba am_pm_background_color_ = 0;
        std::uint32_t am_pm_font_handle_ = 0;
        common::rgba am_pm_text_color_ = 0;
    };

    /**
     * \brief Parse a clock's constructor buffer, which ends on KCheckValueForEndOfTimeDeviceConstructionBuf.
     *
     * \param with_hand_centre_offset Series 80's layout: RAnalogClock::ConstructL's aOffsetHandCenter follows the
     *                                analog display arguments.
     * \returns False if the buffer does not hold a whole clock of that layout.
     */
    bool parse_clock_constructor_args(const std::uint8_t *data, const std::size_t size, const bool with_hand_centre_offset,
        clock_constructor_args &out);

    /**
     * \brief A clock of CLOCKA.DLL: RDigitalClock or RAnalogClock.
     *
     * The client packs the whole clock into one constructor buffer (RTimeDevice::AppendXxxConstructorArgsL): the
     * universal time offset, the display type, the display's rectangle, margins and shadow, then either the digital
     * text sections (font handle, colour, alignment, TTime::FormatL format) or the analog face bitmaps and hands
     * (vector features drawn at 12 o'clock around (0, 0)), ended by a check value.
     */
    struct clock_anim_executor : public anim_executor {
    private:
        window_server *ws_;
        kernel_system *kern_;

        bool constructed_ = false;
        bool visible_ = false;
        bool state_changed_ = true;

        std::int32_t universal_time_offset_ = 0;
        clock_display_type type_ = clock_display_digital;

        eka2l1::vec2 position_{ 0, 0 };
        eka2l1::vec2 size_{ 0, 0 };
        clock_margins margins_;
        clock_shadow shadow_;

        // Digital
        common::rgba background_color_ = 0;
        std::vector<clock_text_section> sections_;

        // Analog
        fbsbitmap *face_ = nullptr;
        fbsbitmap *face_mask_ = nullptr;
        eka2l1::vec2 hand_centre_offset_{ 0, 0 };
        std::vector<clock_hand> hands_;

        bool has_am_pm_ = false;
        eka2l1::vec2 am_pm_position_{ 0, 0 };
        eka2l1::vec2 am_pm_size_{ 0, 0 };
        clock_shadow am_pm_shadow_;
        common::rgba am_pm_background_color_ = 0;
        clock_text_section am_pm_section_;

        // Series 80: the text section the date/time toggle applies to
        std::int32_t date_time_toggle_section_ = -1;

        std::int64_t shown_time_key_ = -1;

        bool updates_every_second() const;
        std::int64_t local_time_seconds() const;
        std::int64_t time_key() const;

        fbsbitmap *take_bitmap(const std::uint32_t handle);
        fbsfont *take_font(const std::uint32_t handle);
        common::rgba to_rgba(const std::uint32_t raw) const;

        void draw_digital(gdi_store_command_segment &segment, const eka2l1::rect &rect, const common::rgba background,
            const std::vector<clock_text_section> &sections, const clock_shadow &shadow, const std::int64_t local_time);

        void draw_analog(gdi_store_command_segment &segment, const std::int64_t local_time);

    public:
        explicit clock_anim_executor(canvas_base *canvas);
        ~clock_anim_executor() override;

        std::int32_t construct(const std::uint8_t *args, const std::size_t args_size) override;
        std::int32_t handle_request(service::ipc_context &ctx, const std::int32_t opcode, const std::uint8_t *args,
            const std::size_t args_size) override;

        bool overlay_dirty() override;
        bool build_overlay(gdi_store_command_segment &segment, eka2l1::rect &bounds) override;
        bool next_overlay_update(std::uint64_t &delay_us) override;

        eka2l1::rect rect_drawn_to() const {
            return eka2l1::rect(position_, size_);
        }
    };

    /**
     * \brief TTime::FormatL for the format strings of digital clocks, with a 24-hour English locale.
     *
     * \param format        The format, a TTime::FormatL string plus the flashing block delimiter (char 1).
     * \param local_seconds Local time, in seconds since 1970.
     */
    std::u16string clock_format_time(const std::u16string &format, const std::int64_t local_seconds, const std::uint32_t microseconds = 0);
}
