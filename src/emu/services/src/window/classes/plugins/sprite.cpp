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

#include <services/window/classes/plugins/sprite.h>
#include <services/window/window.h>
#include <services/window/op.h>
#include <utils/err.h>

namespace eka2l1::epoc {
    bool sprite::execute_command(service::ipc_context &ctx, ws_cmd &cmd) {
        ws_sprite_op op = static_cast<decltype(op)>(cmd.header.op);
        bool quit = false;

        switch (op) {
        case ws_sprite_free: {
            ctx.complete(epoc::error_none);
            client->delete_object(cmd.obj_handle);

            quit = true;
            break;
        }

        case ws_sprite_set_position: {
            if (cmd.header.cmd_len < sizeof(eka2l1::vec2)) {
                ctx.complete(epoc::error_argument);
                break;
            }

            position = *reinterpret_cast<eka2l1::vec2 *>(cmd.data_ptr);
            ctx.complete(epoc::error_none);
            break;
        }

        case ws_sprite_append_member: {
            if (cmd.header.cmd_len < sizeof(ws_cmd_sprite_member)) {
                ctx.complete(epoc::error_argument);
                break;
            }

            members.push_back(*reinterpret_cast<ws_cmd_sprite_member *>(cmd.data_ptr));
            ctx.complete(epoc::error_none);
            break;
        }

        case ws_sprite_update_member:
        case ws_sprite_update_member2: {
            // UpdateMember only asks for a repaint of the given frame; UpdateMember2 also replaces it.
            if (cmd.header.cmd_len < ((op == ws_sprite_update_member2) ? sizeof(ws_cmd_sprite_update_member) : sizeof(std::int32_t))) {
                ctx.complete(epoc::error_argument);
                break;
            }

            ws_cmd_sprite_update_member *update = reinterpret_cast<ws_cmd_sprite_update_member *>(cmd.data_ptr);

            if ((update->index < 0) || (static_cast<std::size_t>(update->index) >= members.size())) {
                ctx.complete(epoc::error_argument);
                break;
            }

            if (op == ws_sprite_update_member2) {
                members[update->index] = update->member;
            }

            ctx.complete(epoc::error_none);
            break;
        }

        case ws_sprite_activate: {
            // CWsSpriteBase::CompleteL panics a client that activates a sprite without members; such a
            // sprite simply stays inactive here.
            active = !members.empty();
            ctx.complete(epoc::error_none);
            break;
        }

        default: {
            // The number of unimplemented is too small, better just complete all
            LOG_ERROR(SERVICE_WINDOW, "Unimplemented SpriteDLL opcode: 0x{:x}", cmd.header.op);
            ctx.complete(epoc::error_none);

            break;
        }
        }

        return quit;
    }

    sprite::sprite(window_server_client_ptr client, screen *scr, window *attached_window, eka2l1::vec2 pos)
        : window_client_obj(client, scr)
        , position(pos)
        , attached_window(attached_window) {
    }
}