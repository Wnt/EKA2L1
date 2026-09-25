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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#include <catch2/catch.hpp>

#include <services/window/s80pane.h>

using namespace eka2l1;

namespace {
    constexpr std::uint32_t HOST_ESCAPE = 0x01000000;
    constexpr std::uint32_t HOST_RETURN = 0x01000004;
    constexpr std::uint32_t HOST_F1 = 0x01000030;
    constexpr std::uint32_t HOST_F4 = 0x01000033;
}

// CEikonEnv's error alert: "System:\nUnable to find the specified object.\n" as the first line, no second line.
TEST_CASE("s80_note_splits_the_alert_title", "window") {
    std::u16string title = u"System:\nUnable to find the specified object.\n";
    std::u16string text;
    epoc::s80_note::split_title(title, text);

    REQUIRE(title == u"System:");
    REQUIRE(text == u"Unable to find the specified object.");

    std::u16string plain_title = u"Low memory";
    std::u16string plain_text = u"Close some applications";
    epoc::s80_note::split_title(plain_title, plain_text);

    REQUIRE(plain_title == u"Low memory");
    REQUIRE(plain_text == u"Close some applications");
}

TEST_CASE("s80_note_is_modal_and_answers_with_the_device_keys", "window") {
    epoc::s80_note note(nullptr);
    int answered = -100;

    REQUIRE_FALSE(note.handle_key_press(HOST_RETURN));

    REQUIRE(note.show(u"Title", u"Text", u"", u"", [&](int b) { answered = b; }));
    REQUIRE(note.active());

    // A second note does not replace the one up.
    REQUIRE_FALSE(note.show(u"Other", u"Text", u"", u"", [&](int) { answered = 42; }));

    // Letters are swallowed, the note stays up.
    REQUIRE(note.handle_key_press('A'));
    REQUIRE(note.active());
    REQUIRE(answered == -100);

    // One button: Esc answers it.
    REQUIRE(note.handle_key_press(HOST_ESCAPE));
    REQUIRE_FALSE(note.active());
    REQUIRE(answered == 0);

    REQUIRE(note.show(u"Title", u"Text", u"Yes", u"No", [&](int b) { answered = b; }));
    REQUIRE(note.handle_key_press(HOST_F4));
    REQUIRE(answered == 1);

    REQUIRE(note.show(u"Title", u"Text", u"Yes", u"No", [&](int b) { answered = b; }));
    REQUIRE(note.handle_key_press(HOST_ESCAPE));
    REQUIRE(answered == 1);

    REQUIRE(note.show(u"Title", u"Text", u"Yes", u"No", [&](int b) { answered = b; }));
    REQUIRE(note.handle_key_press(HOST_F1));
    REQUIRE(answered == 0);
    REQUIRE_FALSE(note.active());
}
