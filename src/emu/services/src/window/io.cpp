/*
 * Copyright (c) 2020 EKA2L1 Team
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

#include <services/window/classes/wingroup.h>
#include <services/window/classes/winuser.h>
#include <services/window/io.h>
#include <services/window/window.h>

#include <kernel/kernel.h>
#include <system/epoc.h>

#include <optional>

namespace eka2l1::epoc {
    void window_pointer_focus_walker::add_new_event(const epoc::event &evt) {
        evts_.emplace_back(evt, false);
    }

    void window_pointer_focus_walker::process_event_to_target_window(epoc::window *win, epoc::event &evt) {
        assert(win->type == epoc::window_kind::client);

        epoc::canvas_base *user = reinterpret_cast<epoc::canvas_base *>(win);
        // Stop, we found it!
        // Send it right now
        evt.adv_pointer_evt_.pos = scr_coord_ - user->absolute_position();

        if (user->parent->type == epoc::window_kind::top_client) {
            evt.adv_pointer_evt_.parent_pos = scr_coord_;
        } else {
            // It must be client kind
            assert(user->parent->type == epoc::window_kind::client);
            evt.adv_pointer_evt_.parent_pos = scr_coord_ - reinterpret_cast<epoc::canvas_base *>(user->parent)->absolute_position();
        }

        evt.handle = win->get_client_handle();

        kernel_system *kern = win->client->get_ws().get_kernel_system();

        kern->lock();
        win->queue_event(evt);
        kern->unlock();
    }

    bool window_pointer_focus_walker::do_it(epoc::window *win) {
        if (evts_.empty()) {
            return true;
        }

        if (win->type != epoc::window_kind::client) {
            return false;
        }

        epoc::canvas_base *user = reinterpret_cast<epoc::canvas_base *>(win);

        std::optional<eka2l1::rect> contain_area;
        auto contain_rect_this_mode_ite = user->scr->pointer_areas_.find(user->scr->crr_mode);

        if (contain_rect_this_mode_ite != user->scr->pointer_areas_.end()) {
            contain_area = contain_rect_this_mode_ite->second;
        }

        for (auto &[evt, sent_to_highest_z] : evts_) {
            // Is this event really in the pointer area, if area exists. If not, pass
            if (contain_area && !contain_area->contains(evt.adv_pointer_evt_.pos)) {
                continue;
            }

            const bool filter_enter_exit = ((evt.type == epoc::event_code::touch_enter) || (evt.type == epoc::event_code::touch_exit))
                && (user->filter & epoc::pointer_filter_type::pointer_enter);

            const bool filter_drag = (evt.adv_pointer_evt_.evtype == epoc::event_type::drag) && (user->filter & epoc::pointer_filter_type::pointer_drag);
            const bool filter_move = (evt.adv_pointer_evt_.evtype == epoc::event_type::move) && (user->filter & epoc::pointer_filter_type::pointer_move);

            // Filter out events, assuming move event never exist (phone)
            // When you use touch on your phone, you drag your finger. Move your mouse (2023 correct:) is on PC (~~simply doesn't exist~~ is on mobile).
            if (filter_enter_exit || filter_drag || filter_move) {
                continue;
            }

            if (!sent_to_highest_z) {
                if (user->visible_region.contains(evt.adv_pointer_evt_.pos)) {
                    scr_coord_ = evt.adv_pointer_evt_.pos;

                    process_event_to_target_window(win, evt);
                    sent_to_highest_z = true;
                }

                continue;
            } else {
                // Check to see if capture flag is enabled
                // TODO:
            }
        }

        return false;
    }

    void window_pointer_focus_walker::clear() {
        evts_.clear();
    }

    window_key_shipper::window_key_shipper(window_server *serv)
        : serv_(serv) {
    }

    void window_key_shipper::add_new_event(const epoc::event &evt) {
        evts_.push_back(evt);
    }

    static const bool is_device_std_key_not_repeatable(const std_scan_code code) {
        return (code >= std_key_device_0) && (code <= std_key_device_1);
    }

    // Series 80 keyboard stopgap. EKA2L1 does not run the ROM's EKTRAN/EKDATA key translation, and the
    // fixed table in map_scancode_to_keycode() types every letter in upper case and nothing at all for the
    // punctuation scan codes (0x78..0x93). Until that translation is ported, an S80 device gets only what
    // typing text needs:
    //  - a binding target >= 0x10000 is a character binding: the low 16 bits are the scan code, the high
    //    16 bits the key code it types (0x003F007B is '?' on EStdKeyForwardSlash), whatever the state of
    //    the host's Shift key, so the host keyboard layout does not matter;
    //  - a letter takes its case from Shift xor Caps Lock, and Ctrl+letter types its control code;
    //  - Shift, Ctrl, Chr (Func) and Caps Lock are tracked, stamped on every key event, and produce no
    //    EEventKey;
    //  - the keys that the ROM's EKDATA lists as non-autorepeating do not repeat.
    static std::uint32_t s80_modifier_of(const std::uint32_t scancode) {
        switch (scancode) {
        case std_key_left_shift:
            return event_modifier_left_shift | event_modifier_shift;

        case std_key_left_ctrl:
            return event_modifier_left_ctrl | event_modifier_ctrl;

        case std_key_left_func:
            return event_modifier_left_func | event_modifier_func;

        case std_key_right_func:
            return event_modifier_right_func | event_modifier_func;

        case std_key_caps_lock:
            return event_modifier_caps_lock;

        default:
            break;
        }

        return 0;
    }

    static bool is_s80_std_key_repeatable(const std::uint32_t code) {
        return (code != std_key_escape) && (code != std_key_menu) && !s80_modifier_of(code) && ((code < std_key_device_0) || (code > std_key_device_3)) && ((code < std_key_application_0) || (code > std_key_application_7));
    }

    // The auto-repeat that the last S80 key down started. Any key up ends it, because the host may report the
    // released key as another key than the pressed one (Shift already let go: '?' goes down, '/' comes up).
    static std::optional<std::uint64_t> s80_repeat_data;

    // Returns the key code the event types, or 0 to take it from map_scancode_to_keycode().
    static std::uint32_t s80_translate_key(epoc::event &evt, std::uint32_t &modifiers) {
        const std::uint32_t target = static_cast<std::uint32_t>(evt.key_evt_.scancode);
        const std::uint32_t scancode = target & 0xFFFF;
        const std::uint32_t modifier = s80_modifier_of(scancode);

        std::uint32_t code = target >> 16;

        if (modifier == event_modifier_caps_lock) {
            if (evt.type == epoc::event_code::key_down) {
                modifiers ^= modifier;
            }
        } else if (modifier) {
            if (evt.type == epoc::event_code::key_down) {
                modifiers |= modifier;
            } else {
                modifiers &= ~modifier;
            }
        } else if (!code && (scancode >= 'A') && (scancode <= 'Z')) {
            const bool upper = ((modifiers & event_modifier_shift) != 0) != ((modifiers & event_modifier_caps_lock) != 0);

            if (modifiers & event_modifier_ctrl) {
                code = scancode - 'A' + 1;
            } else {
                code = upper ? scancode : (scancode + ('a' - 'A'));
            }
        }

        evt.key_evt_.scancode = static_cast<std::int32_t>(scancode);
        evt.key_evt_.modifiers = modifiers;

        return code;
    }

    void window_key_shipper::start_shipping() {
        if (evts_.empty()) {
            return;
        }

        epoc::window_group *focus = serv_->get_focus();

        if (!focus) {
            return;
        }

        int ui_rotation = focus->scr->ui_rotation;
        const bool is_s80 = serv_->get_system()->is_s80_device_active();

        for (auto &evt : evts_) {
            const std::uint32_t s80_code = is_s80 ? s80_translate_key(evt, s80_modifiers_) : 0;

            evt.key_evt_.scancode = epoc::post_processing_scancode(static_cast<epoc::std_scan_code>(evt.key_evt_.scancode),
                ui_rotation);

            bool dont_send_extra_key_event = (evt.type != epoc::event_code::key_down) || (is_s80 && s80_modifier_of(evt.key_evt_.scancode));

            // TODO: My assumption... For now.
            // Actually this smells like a hack
            const bool repeatable = is_s80 ? is_s80_std_key_repeatable(evt.key_evt_.scancode) : !is_device_std_key_not_repeatable(static_cast<epoc::std_scan_code>(evt.key_evt_.scancode));

            epoc::event extra_event = evt;
            extra_event.type = epoc::event_code::key;

            kernel_system *kern = focus->client->get_ws().get_kernel_system();
            ntimer *timing = kern->get_ntimer();

            auto cancel_repeat = [&](std::optional<std::uint64_t> &data) {
                if (!data) {
                    return;
                }

                kern->lock();

                if (!timing->unschedule_event(serv_->repeatable_event_, *data)) {
                    serv_->cancel_repeatable_list.insert(*data);
                }

                kern->unlock();
                data.reset();
            };

            const std::uint32_t the_code = s80_code ? s80_code : epoc::map_scancode_to_keycode(static_cast<std_scan_code>(evt.key_evt_.scancode));

            const std::uint64_t data_for_repeatable = extra_event.key_evt_.scancode | (static_cast<std::uint64_t>(the_code) << 32);

            if (!dont_send_extra_key_event) {
                extra_event.key_evt_.code = the_code;
                extra_event.time = kern->universal_time();

                if (repeatable)
                    extra_event.key_evt_.modifiers |= event_modifier_repeatable;
            }

            // A captured key goes to the winning capturer instead of the focus: CaptureKey requests are
            // keyed by key code and win the translated key event, CaptureKeyUpAndDowns requests are keyed
            // by scan code and win the up/down events. (The raw event's own code field is always 0, so
            // it must not be the lookup key.)
            auto target_for = [&](const std::uint32_t code, const epoc::event_key_capture_type type) -> epoc::window * {
                auto found = serv_->key_capture_requests.find(code);

                if (found == serv_->key_capture_requests.end()) {
                    return focus;
                }

                const epoc::event_capture_key_notifier *capture = find_key_capture(found->second, type, evt.key_evt_.modifiers);
                return capture ? capture->user : focus;
            };

            epoc::window *raw_target = target_for(static_cast<std::uint32_t>(evt.key_evt_.scancode), epoc::event_key_capture_type::up_and_downs);
            epoc::window *key_target = target_for(the_code, epoc::event_key_capture_type::normal);

            evt.handle = raw_target->get_client_handle();
            extra_event.handle = key_target->get_client_handle();

            kern->lock();
            raw_target->queue_event(evt);
            kern->unlock();

            if (!dont_send_extra_key_event) {
                // Give it a single key event also
                kern->lock();
                key_target->queue_event(extra_event);
                kern->unlock();

                if ((evt.type == epoc::event_code::key_down) && repeatable) {
                    if (is_s80) {
                        cancel_repeat(s80_repeat_data);
                        s80_repeat_data = data_for_repeatable;
                    }

                    timing->schedule_event(serv_->initial_repeat_delay_, serv_->repeatable_event_, data_for_repeatable);
                }
            }

            if (is_s80 && (evt.type == epoc::event_code::key_up)) {
                cancel_repeat(s80_repeat_data);
            } else if ((evt.type == epoc::event_code::key_up) && repeatable) {
                std::optional<std::uint64_t> data = data_for_repeatable;
                cancel_repeat(data);
            }
        }

        evts_.clear();
    }
}
