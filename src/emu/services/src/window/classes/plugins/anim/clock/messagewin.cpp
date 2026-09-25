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

#include <services/window/classes/plugins/anim/clock/messagewin.h>
#include <services/window/classes/winuser.h>

#include <kernel/kernel.h>
#include <kernel/process.h>
#include <mem/ptr.h>
#include <system/epoc.h>
#include <utils/des.h>

#include <common/log.h>
#include <utils/err.h>

#include <cstring>

namespace eka2l1::epoc {
    messagewin_anim_executor::messagewin_anim_executor(canvas_base *canvas)
        : anim_executor(canvas) {
        canvas->set_visible(false);
    }

    // CLOCKA.DLL's DMessageWindow (clockanim MSGWIN.CPP, TMessageWindowCommand).
    enum messagewin_command {
        messagewin_start_display = 0,
        messagewin_cancel_display = 1,
        messagewin_get_borders = 2,
        messagewin_set_background_color = 3,
        messagewin_set_text_color = 4,
        messagewin_set_border_color = 5,
        messagewin_set_plinth_color = 6
    };

    std::int32_t messagewin_anim_executor::handle_request(service::ipc_context &ctx, const std::int32_t opcode, const std::uint8_t *args,
        const std::size_t args_size) {
        switch (opcode) {
        case messagewin_get_borders: {
            // TMargins { iLeft, iRight, iTop, iBottom } = EBorderWidthLeft/Right/Top/Bottom. The client sizes and
            // places its message window from these; left unanswered it read stack garbage and extended the window
            // to x = -1345068182 (found by W2).
            static constexpr std::int32_t borders[4] = { 3, 4, 3, 4 };

            if (ctx.sys->get_kernel_system()->is_eka1() && args && (args_size >= sizeof(std::uint32_t))) {
                // EKA1: the argument is a pointer to the client's TPckg<TMargins>.
                std::uint32_t target_address = 0;
                std::memcpy(&target_address, args, sizeof(target_address));

                kernel::process *pr = ctx.msg->own_thr->owning_process();
                epoc::des8 *target = eka2l1::ptr<epoc::des8>(target_address).get(pr);

                if (!target || (target->assign(pr, reinterpret_cast<const std::uint8_t *>(borders), sizeof(borders)) != 0)) {
                    return epoc::error_argument;
                }
            } else if (!ctx.write_data_to_descriptor_argument(1, reinterpret_cast<const std::uint8_t *>(borders), sizeof(borders))) {
                return epoc::error_argument;
            }

            return epoc::error_none;
        }

        case messagewin_start_display:
        case messagewin_cancel_display:
            // The window stays hidden: the busy/info message is not drawn here.
            LOG_TRACE(SERVICE_WINDOW, "Message window {} (the message is not drawn)",
                (opcode == messagewin_start_display) ? "start display" : "cancel display");
            return epoc::error_none;

        case messagewin_set_background_color:
        case messagewin_set_text_color:
        case messagewin_set_border_color:
        case messagewin_set_plinth_color:
            return epoc::error_none;

        default:
            LOG_WARN(SERVICE_WINDOW, "Unknown message window animation command {} ({} bytes of arguments)", opcode, args_size);
            return epoc::error_none;
        }
    }

    messagewin_anim_executor::~messagewin_anim_executor() {
        if (canvas_) {
            canvas_->set_visible(true);
        }
    }
}