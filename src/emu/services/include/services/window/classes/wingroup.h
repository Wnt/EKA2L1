/*
 * Copyright (c) 2019 EKA2L1 Team
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

#include <drivers/graphics/graphics.h>
#include <services/window/classes/winbase.h>
#include <config/app_settings.h>

#include <vector>
#include <queue>

namespace eka2l1::kernel {
    class process;
}

namespace eka2l1::epoc {
    struct top_canvas;
    struct canvas_base;
    using message_data = std::vector<std::uint8_t>;

    // The group's text cursor (RWsTextCursor). Position and size are the drawn rectangle,
    // relative to the cursor window's origin: pos.y already has the ascent taken off.
    struct text_cursor {
        enum {
            type_none = 0,
            type_rectangle = 1,
            type_hollow_rectangle = 2
        };

        enum {
            flag_no_flash = 1 << 0
        };

        std::int32_t type = type_none;
        std::uint32_t window_handle = 0;
        std::uint32_t window_id = 0;
        eka2l1::vec2 pos{ 0, 0 };
        eka2l1::vec2 size{ 0, 0 };
        std::uint32_t flags = 0;
        std::uint32_t color = 0;
        bool clipped = false;
        eka2l1::rect clip_rect;

        bool operator==(const text_cursor &rhs) const {
            return (type == rhs.type) && (window_handle == rhs.window_handle) && (window_id == rhs.window_id)
                && (pos == rhs.pos) && (size == rhs.size) && (flags == rhs.flags) && (color == rhs.color)
                && (clipped == rhs.clipped) && (!clipped || ((clip_rect.top == rhs.clip_rect.top) && (clip_rect.size == rhs.clip_rect.size)));
        }
    };

    struct window_group : public epoc::window {
        std::u16string name;
        std::unique_ptr<top_canvas> top;
        std::queue<message_data> msg_datas;
        text_cursor cursor;

        eka2l1::config::app_setting saved_setting;
        std::size_t uid_owner_change_callback_handle;

        kernel::process *uid_owner_change_process;
        // The same process by its kernel id. It can die before this group does -
        // an application started by another outlives its starter - and then the
        // pointer above is freed memory; the id says whether it is still there.
        kernel::uid uid_owner_change_process_id;
        ws::uid screen_change_event_handle;

        bool can_receive_focus() {
            return flags & flag_focus_receiveable;
        }

        void set_receive_focus(const bool val) {
            flags &= ~flag_focus_receiveable;

            if (val)
                flags |= flag_focus_receiveable;
        }

        explicit window_group(window_server_client_ptr client, screen *scr, epoc::window *parent,
            const std::uint32_t client_handle);

        ~window_group() override;

        void on_owner_process_uid_type_change(const std::uint32_t new_uid);
        void set_name(const std::u16string &new_name);

        // The client window carrying the cursor, or null (and the cursor dropped) when it is gone.
        canvas_base *text_cursor_window();
        bool text_cursor_flashing() const;
        void text_cursor_changed();

        // ===================== COMMAND OPCODES =======================
        void set_text_cursor(service::ipc_context &context, ws_cmd &cmd, const bool clipped);
        void cancel_text_cursor(service::ipc_context &context, ws_cmd &cmd);
        void receive_focus(service::ipc_context &context, ws_cmd &cmd);
        void add_priority_key(service::ipc_context &context, ws_cmd &cmd);
        void set_name(service::ipc_context &context, ws_cmd &cmd);
        void destroy(service::ipc_context &context, ws_cmd &cmd);
        bool execute_command(service::ipc_context &context, ws_cmd &cmd) override;

        void lost_focus();
        void gain_focus();

        void queue_message_data(const std::uint8_t *data, const std::size_t data_size);
        void get_message_data(std::uint8_t *data, std::size_t &dest_size);
    };
}