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

// A client's event queue must never lose what the user typed. Typing faster than an app reads its
// events filled the 32-entry queue, and the purge then threw away EEventKey events (the characters)
// while keeping their up/down events, so words lost letters.

#include <services/window/fifo.h>

#include <catch2/catch.hpp>

#include <string>
#include <vector>

using namespace eka2l1;

namespace {
    epoc::event key_event(const epoc::event_code type, const std::uint32_t code) {
        epoc::event evt(1, type);
        evt.key_evt_.code = code;
        evt.key_evt_.scancode = static_cast<std::int32_t>(code);
        evt.key_evt_.modifiers = 0;
        evt.key_evt_.repeats = 0;
        return evt;
    }

    epoc::event pointer_event(const epoc::event_type type) {
        epoc::event evt(1, epoc::event_code::touch);
        evt.adv_pointer_evt_.evtype = type;
        evt.adv_pointer_evt_.ptr_num = 0;
        return evt;
    }

    void type_text(epoc::event_fifo &fifo, const std::string &text) {
        for (const char ch : text) {
            fifo.queue_event(key_event(epoc::event_code::key_down, static_cast<std::uint32_t>(ch)));
            fifo.queue_event(key_event(epoc::event_code::key, static_cast<std::uint32_t>(ch)));
            fifo.queue_event(key_event(epoc::event_code::key_up, static_cast<std::uint32_t>(ch)));
        }
    }

    std::vector<epoc::event> drain(epoc::event_fifo &fifo) {
        std::vector<epoc::event> out;
        while (auto evt = fifo.get_evt_opt()) {
            out.push_back(*evt);
        }
        return out;
    }
}

TEST_CASE("event_fifo_keeps_every_key_past_its_nominal_size", "[window][fifo]") {
    epoc::event_fifo fifo;
    const std::string text = "Hello World =A1+A2 The quick brown fox jumps over 13 lazy dogs? (OK!)";

    type_text(fifo, text);

    const std::vector<epoc::event> events = drain(fifo);
    REQUIRE(events.size() == text.size() * 3);

    std::string typed;
    for (std::size_t i = 0; i < events.size(); i++) {
        const epoc::event_code expected[3] = { epoc::event_code::key_down, epoc::event_code::key, epoc::event_code::key_up };
        REQUIRE(events[i].type == expected[i % 3]);

        if (events[i].type == epoc::event_code::key) {
            typed += static_cast<char>(events[i].key_evt_.code);
        }
    }

    REQUIRE(typed == text);
}

TEST_CASE("event_fifo_purges_pointer_moves_before_growing", "[window][fifo]") {
    epoc::event_fifo fifo;

    // A drag, then enough keys to fill the queue: the drag goes, every key stays.
    fifo.queue_event(pointer_event(epoc::event_type::drag));
    fifo.queue_event(epoc::event(1, epoc::event_code::null));

    type_text(fifo, "abcdefghijk"); // 33 key events: the queue is full after 30

    const std::vector<epoc::event> events = drain(fifo);
    REQUIRE(events.size() == 33);

    for (const epoc::event &evt : events) {
        REQUIRE(evt.type != epoc::event_code::touch);
        REQUIRE(evt.type != epoc::event_code::null);
    }
}

TEST_CASE("event_fifo_stops_at_its_hard_cap", "[window][fifo]") {
    epoc::event_fifo fifo;

    for (std::size_t i = 0; i < epoc::event_fifo::hard_maximum_element + 10; i++) {
        fifo.queue_event(key_event(epoc::event_code::key, 'x'));
    }

    REQUIRE(drain(fifo).size() == epoc::event_fifo::hard_maximum_element);
}
