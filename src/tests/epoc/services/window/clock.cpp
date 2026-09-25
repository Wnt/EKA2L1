/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <catch2/catch.hpp>
#include <services/window/classes/plugins/anim/clock/clock.h>

#include <cstring>
#include <vector>

using namespace eka2l1;

namespace {
    struct buffer_writer {
        std::vector<std::uint8_t> bytes_;

        void i32(const std::int32_t value) {
            const std::size_t at = bytes_.size();
            bytes_.resize(at + 4);
            std::memcpy(bytes_.data() + at, &value, 4);
        }

        void point(const std::int32_t x, const std::int32_t y) {
            i32(x);
            i32(y);
        }

        void text(const std::u16string &text) {
            const std::size_t at = bytes_.size();
            const std::size_t bytes = text.length() * 2;
            bytes_.resize(at + ((bytes + 3) & ~static_cast<std::size_t>(3)), 0);
            std::memcpy(bytes_.data() + at, text.data(), bytes);
        }
    };

    // RTimeDevice's display arguments: position, size, margins, shadow
    void write_display(buffer_writer &writer) {
        writer.point(10, 20);
        writer.point(64, 48);
        writer.i32(1);
        writer.i32(2);
        writer.i32(3);
        writer.i32(4);
        writer.i32(1);
        writer.i32(0x808080);
        writer.point(1, 1);
    }
}

TEST_CASE("Clock anim parses a digital clock constructor buffer", "[window][clock]") {
    buffer_writer writer;
    writer.i32(7200); // universal time offset
    writer.i32(epoc::clock_display_digital);
    write_display(writer);
    writer.i32(0x00FFFFFF); // background
    writer.i32(1); // one text section
    writer.i32(0x1234); // font handle
    writer.i32(0x000000FF); // red text (EKA1 TRgb)
    writer.i32(2); // right aligned
    writer.i32(2); // centred, descent excluded
    writer.i32(5);
    writer.i32(0);
    const std::u16string format = u"%J%:1%T";
    writer.i32(static_cast<std::int32_t>(format.length()));
    writer.text(format);
    writer.i32(0x3af96b5e);

    epoc::clock_constructor_args args;
    REQUIRE(epoc::parse_clock_constructor_args(writer.bytes_.data(), writer.bytes_.size(), false, args));
    REQUIRE(args.universal_time_offset_ == 7200);
    REQUIRE(args.type_ == epoc::clock_display_digital);
    REQUIRE(args.position_ == eka2l1::vec2(10, 20));
    REQUIRE(args.size_ == eka2l1::vec2(64, 48));
    REQUIRE(args.margins_.left_ == 1);
    REQUIRE(args.margins_.bottom_ == 4);
    REQUIRE(args.shadow_.on_);
    REQUIRE(args.sections_.size() == 1);
    REQUIRE(args.sections_[0].font_handle_ == 0x1234);
    REQUIRE(args.sections_[0].horizontal_alignment_ == 2);
    REQUIRE(args.sections_[0].format_ == format);

    // A buffer cut short, or without its check value, is refused
    REQUIRE_FALSE(epoc::parse_clock_constructor_args(writer.bytes_.data(), writer.bytes_.size() - 4, false, args));
}

TEST_CASE("Clock anim parses an analog clock with hands and the Series 80 hand-centre offset", "[window][clock]") {
    for (const bool series80 : { false, true }) {
        buffer_writer writer;
        writer.i32(0);
        writer.i32(epoc::clock_display_analog);
        writer.point(10, 20);
        writer.point(64, 48);
        writer.i32(1);
        writer.i32(2);
        writer.i32(3);
        writer.i32(4);

        if (series80) {
            // Series 80: the hand-centre offset sits between the margins and the shadow
            writer.point(0, 3);
        }

        writer.i32(1);
        writer.i32(0xaaaaaa);
        writer.point(2, 2);
        writer.i32(0x100); // face
        writer.i32(0x101); // face mask
        writer.i32(2); // hands
        writer.i32(0); // no am/pm

        // Hour hand: one line
        writer.i32(epoc::clock_hand_one_rev_per_12_hours);
        writer.i32(1);
        writer.i32(epoc::clock_hand_feature_line);
        writer.i32(1);
        writer.i32(0);
        writer.point(3, 3);
        writer.point(0, 0);
        writer.point(0, -12);

        // Minute hand: a closed polygon and a circle
        writer.i32(epoc::clock_hand_one_rev_per_hour);
        writer.i32(2);
        writer.i32(epoc::clock_hand_feature_polyline);
        writer.i32(1);
        writer.i32(0);
        writer.point(1, 1);
        writer.i32(1);
        writer.i32(0);
        writer.i32(1);
        writer.i32(3);
        writer.point(-2, 0);
        writer.point(0, -20);
        writer.point(2, 0);
        writer.i32(epoc::clock_hand_feature_circle);
        writer.i32(1);
        writer.i32(0);
        writer.point(1, 1);
        writer.i32(1);
        writer.i32(0);
        writer.point(0, 0);
        writer.i32(2);

        writer.i32(0x3af96b5e);

        epoc::clock_constructor_args args;
        REQUIRE(epoc::parse_clock_constructor_args(writer.bytes_.data(), writer.bytes_.size(), series80, args));
        REQUIRE(args.type_ == epoc::clock_display_analog);
        REQUIRE(args.face_handle_ == 0x100);
        REQUIRE(args.face_mask_handle_ == 0x101);
        REQUIRE(args.hands_.size() == 2);
        REQUIRE(args.hands_[0].features_.size() == 1);
        REQUIRE(args.hands_[0].features_[0].points_[1] == eka2l1::vec2(0, -12));
        REQUIRE(args.hands_[1].features_.size() == 2);
        REQUIRE(args.hands_[1].features_[0].closed_);
        REQUIRE(args.hands_[1].features_[0].points_.size() == 3);
        REQUIRE(args.hands_[1].features_[1].radius_ == 2);
        REQUIRE(args.hand_centre_offset_ == (series80 ? eka2l1::vec2(0, 3) : eka2l1::vec2(0, 0)));

        // The other layout does not end on the check value
        REQUIRE_FALSE(epoc::parse_clock_constructor_args(writer.bytes_.data(), writer.bytes_.size(), !series80, args));
    }
}

namespace {
    std::vector<std::uint8_t> words_to_bytes(const std::vector<std::uint32_t> &words) {
        std::vector<std::uint8_t> bytes(words.size() * 4);
        std::memcpy(bytes.data(), words.data(), bytes.size());
        return bytes;
    }
}

TEST_CASE("Clock anim parses the constructor buffers the Nokia 9300 sends", "[window][clock]") {
    // The status pane's digital clock: three sections, "%-B", "%J%:1%T", "%+B"
    const std::vector<std::uint8_t> status_pane = words_to_bytes({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00ffffff, 0, 0,
        0x00ad9a9c, 3, 4, 0x00ffffff, 0, 4, 0, 2, 3, 0x002d0025, 0x00000042, 4, 0x00ffffff, 1, 4, 0, 2, 7, 0x004a0025,
        0x003a0025, 0x00250031, 0x00000054, 4, 0x00ffffff, 2, 4, 0, 2, 3, 0x002b0025, 0x00000042, 0x3af96b5e });

    epoc::clock_constructor_args args;
    REQUIRE(epoc::parse_clock_constructor_args(status_pane.data(), status_pane.size(), false, args));
    REQUIRE(args.type_ == epoc::clock_display_digital);
    REQUIRE(args.background_color_ == 0x00ad9a9c);
    REQUIRE(args.sections_.size() == 3);
    REQUIRE(args.sections_[1].format_ == u"%J%:1%T");
    REQUIRE(args.sections_[2].horizontal_alignment_ == 2);

    // The Clock app's analog face: three single-line hands, Series 80 layout
    const std::vector<std::uint8_t> analog = words_to_bytes({ 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x00aaaaaa, 2, 2,
        0x343, 0x344, 3, 0, 0, 1, 0, 1, 0, 3, 3, 0, 4, 0, 0xfffffff2, 1, 1, 0, 1, 0, 3, 3, 0, 4, 0, 0xffffffeb, 2, 1, 0, 1,
        0, 1, 1, 0, 4, 0, 0xffffffe7, 0x3af96b5e });

    REQUIRE_FALSE(epoc::parse_clock_constructor_args(analog.data(), analog.size(), false, args));
    REQUIRE(epoc::parse_clock_constructor_args(analog.data(), analog.size(), true, args));
    REQUIRE(args.type_ == epoc::clock_display_analog);
    REQUIRE(args.shadow_.on_);
    REQUIRE(args.shadow_.color_ == 0x00aaaaaa);
    REQUIRE(args.shadow_.offset_ == eka2l1::vec2(2, 2));
    REQUIRE(args.face_handle_ == 0x343);
    REQUIRE(args.face_mask_handle_ == 0x344);
    REQUIRE(args.hands_.size() == 3);
    REQUIRE(args.hands_[0].features_[0].pen_size_ == eka2l1::vec2(3, 3));
    REQUIRE(args.hands_[0].features_[0].points_[1] == eka2l1::vec2(0, -14));
    REQUIRE(args.hands_[1].features_[0].points_[1] == eka2l1::vec2(0, -21));
    REQUIRE(args.hands_[2].type_ == epoc::clock_hand_one_rev_per_minute);
    REQUIRE(args.hands_[2].features_[0].points_[1] == eka2l1::vec2(0, -25));
}

TEST_CASE("Clock anim formats time like TTime::FormatL with a 24-hour English locale", "[window][clock]") {
    // Tuesday 16 November 2004, 12:57:08
    const std::int64_t when = 1100609828;

    REQUIRE(epoc::clock_format_time(u"%J%:1%T", when) == u"12:57");
    REQUIRE(epoc::clock_format_time(u"%H%:1%T%:2%S", when) == u"12:57:08");
    REQUIRE(epoc::clock_format_time(u"%-B%:0%J%:1%T%:3%+B", when) == u"12:57");
    REQUIRE(epoc::clock_format_time(u"%E %*D%X %N %Y", when) == u"Tuesday 16th November 2004");
    REQUIRE(epoc::clock_format_time(u"%*E %D/%M/%*Y", when) == u"Tue 16/11/04");
    REQUIRE(epoc::clock_format_time(u"%*I%:1%T", when + 3600) == u"1:57");
    REQUIRE(epoc::clock_format_time(u"12\x01:\x01" u"57", when) == u"12:57");
}
