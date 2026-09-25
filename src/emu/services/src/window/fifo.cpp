/*
 * Copyright (c) 2019 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 * 
 * Initial contributor: pent0
 * Contributors:
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

#include <common/log.h>
#include <services/window/fifo.h>
#include <common/region.h>


namespace eka2l1::epoc {
    bool event_fifo::is_my_priority_really_high(epoc::event_code evt) {
        switch (evt) {
        case event_code::switch_off:
        case event_code::switch_on:
        case event_code::event_password:
            return true;

        default:
            return false;
        }
    }

    std::uint32_t event_fifo::queue_event(const event &evt) {
        const std::lock_guard<std::mutex> guard(lock_);

        if ((q_.size() >= maximum_element) && !do_purge()) {
            // Nothing in the queue may be thrown away: keys (and pointer presses) are what the user did,
            // and losing one mid-word loses a character or, worse, a Shift release. The queue grows past
            // its nominal size instead, up to a hard cap that only an app that stopped reading reaches.
            if (q_.size() >= hard_maximum_element) {
                LOG_ERROR(SERVICE_WINDOW, "Event queue of {} events is full and nothing can be purged; event type {} dropped",
                    q_.size(), static_cast<int>(evt.type));
                return static_cast<std::uint32_t>(q_.size());
            }
        }

        if ((evt.type == epoc::event_code::touch) && ((evt.adv_pointer_evt_.evtype == epoc::event_type::drag) ||
            (evt.adv_pointer_evt_.evtype == epoc::event_type::move))) {
            if (!q_.empty()) {
                epoc::event &evt_last = q_.back().evt;
                
                // Same delivery
                if ((evt_last.handle == evt.handle) && (evt_last.type == evt.type) && (evt_last.adv_pointer_evt_.evtype == evt.adv_pointer_evt_.evtype)
                    && (evt_last.adv_pointer_evt_.ptr_num == evt.adv_pointer_evt_.ptr_num)) {
                    evt_last = evt;
                    return static_cast<std::uint32_t>(q_.size());;
                }
            }
        }

        std::uint32_t result = queue_event_dont_care(evt);
        trigger_notification();

        return result;
    }

    // What may go when the queue is full, one event at a time and cheapest first (after the window
    // server's EVQUEUE.CPP purge order, minus the input the user produced):
    //   null events; pointer moves and drags (the next one supersedes them); pointer enter/exit;
    //   a focus lost/gained pair; a repeated switch-on.
    // Never purged: key, key-up and key-down events (dropping one loses a character, or leaves a
    // modifier held for the app), pointer presses and releases, and anything not listed above.
    bool event_fifo::do_purge() {
        auto erase_first = [this](auto pred) -> bool {
            for (std::size_t i = 0; i < q_.size(); i++) {
                if (pred(i)) {
                    q_.erase(q_.begin() + i);
                    return true;
                }
            }

            return false;
        };

        if (erase_first([this](std::size_t i) { return q_[i].evt.type == epoc::event_code::null; })) {
            return true;
        }

        if (erase_first([this](std::size_t i) {
                const epoc::event &e = q_[i].evt;
                return (e.type == epoc::event_code::touch) && ((e.adv_pointer_evt_.evtype == epoc::event_type::drag) ||
                    (e.adv_pointer_evt_.evtype == epoc::event_type::move));
            })) {
            return true;
        }

        if (erase_first([this](std::size_t i) {
                return (q_[i].evt.type == epoc::event_code::touch_enter) || (q_[i].evt.type == epoc::event_code::touch_exit);
            })) {
            return true;
        }

        auto is_focus = [](const epoc::event &e) {
            return (e.type == epoc::event_code::focus_gained) || (e.type == epoc::event_code::focus_lost);
        };

        for (std::size_t i = 0; i + 1 < q_.size(); i++) {
            if (is_focus(q_[i].evt) && is_focus(q_[i + 1].evt)) {
                q_.erase(q_.begin() + i, q_.begin() + i + 2);
                return true;
            }
        }

        for (std::size_t i = 0; i + 1 < q_.size(); i++) {
            if ((q_[i].evt.type == epoc::event_code::switch_on) && (q_[i + 1].evt.type == epoc::event_code::switch_on)) {
                q_.erase(q_.begin() + i);
                return true;
            }
        }

        return false;
    }

    event event_fifo::get_event() {
        std::optional<event> evt = get_evt_opt();

        if (!evt) {
            // Create a null event
            return event(0, event_code::null);
        }

        return *evt;
    }

    std::uint32_t redraw_fifo::queue_event(void *owner, const redraw_event &evt, const std::uint16_t pri) {
        const std::lock_guard<std::mutex> guard(lock_);
        eka2l1::rect target_queue_rect(evt.top_left, evt.bottom_right);
        target_queue_rect.transform_from_symbian_rectangle();

        std::size_t limit = q_.size();

        for (std::size_t i = 0; i < limit; i++) {
            eka2l1::rect queued_rect(q_[i].evt.evt_.top_left, q_[i].evt.evt_.bottom_right);
            queued_rect.transform_from_symbian_rectangle();

            if ((q_[i].evt.evt_.handle == evt.handle) && target_queue_rect.contains(queued_rect)) {
                // The new redraw rect contains the old queued redraw rect. Remove it to avoid
                // unneccessary redraws.
                q_.erase(q_.begin() + i);
                limit--;
            }
        }

        redraw_event_full full_event;
        full_event.owner_ = owner;
        full_event.evt_ = evt;

        std::uint32_t id = queue_event_dont_care(full_event);
        q_.back().pri = pri;

        std::stable_sort(q_.begin(), q_.end(),
            [&](const fifo_element &e1, const fifo_element &e2) {
                return e1.pri < e2.pri;
            });

        // Queue a redraw won't directly trigger a notification.
        return id;
    }

    std::optional<redraw_event_full> redraw_fifo::get_visible_evt_opt(const std::function<bool(const redraw_event_full &)> &hidden) {
        const std::lock_guard<std::mutex> guard(lock_);
        while (!q_.empty()) {
            redraw_event_full evt = q_.front().evt;
            q_.erase(q_.begin());

            if (!hidden || !hidden(evt)) {
                return evt;
            }
        }

        return std::nullopt;
    }

    bool redraw_covered_by(const redraw_event &evt, const eka2l1::vec2 &window_abs_top, const std::vector<eka2l1::rect> &covers) {
        eka2l1::rect target(evt.top_left, evt.bottom_right);
        target.transform_from_symbian_rectangle();

        if ((target.size.x <= 0) || (target.size.y <= 0) || covers.empty()) {
            return false;
        }

        target.top += window_abs_top;

        common::region left;
        left.add_rect(target);

        for (const eka2l1::rect &cover : covers) {
            left.eliminate(cover);
            if (left.empty()) {
                return true;
            }
        }

        return left.empty();
    }

    void redraw_fifo::remove_events(void *owner) {
        const std::lock_guard<std::mutex> guard(lock_);
        common::erase_elements(q_, [owner](fifo_element &elem) -> bool {
            return elem.evt.owner_ == owner;
        });
    }
}