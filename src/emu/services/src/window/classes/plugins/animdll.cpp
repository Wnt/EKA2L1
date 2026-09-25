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

#include <services/window/classes/plugins/animdll.h>
#include <services/window/classes/winuser.h>
#include <services/window/screen.h>
#include <services/window/window.h>
#include <services/window/op.h>

#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/timing.h>
#include <mem/ptr.h>
#include <system/epoc.h>
#include <utils/des.h>

#include <common/log.h>
#include <utils/err.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

namespace eka2l1::epoc {
    // EKA2L1_ANIM_TRACE=1 logs every anim DLL request with its argument bytes: the way to learn an anim DLL's
    // command set (numbers, argument layouts) from what its client sends.
    static bool anim_trace_enabled() {
        static const bool enabled = (std::getenv("EKA2L1_ANIM_TRACE") != nullptr);
        return enabled;
    }

    static std::string anim_hex(const void *data, const std::size_t size) {
        static const char *digits = "0123456789abcdef";
        std::string out;
        const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(data);
        for (std::size_t i = 0; i < size; i++) {
            if (i && ((i % 4) == 0)) {
                out += ' ';
            }
            out += digits[bytes[i] >> 4];
            out += digits[bytes[i] & 0xF];
        }
        return out;
    }

    static void anim_trace(ws_cmd &cmd) {
        LOG_INFO(SERVICE_WINDOW, "Anim DLL op {} object 0x{:X} length {}: {}", cmd.header.op, cmd.obj_handle, cmd.header.cmd_len,
            anim_hex(cmd.data_ptr, cmd.header.cmd_len));
    }

    anim_executor::anim_executor(canvas_base *canvas)
        : canvas_(canvas)
        , screen_(canvas ? canvas->scr : nullptr) {
        if (canvas_) {
            canvas_->add_canvas_observer(this);
            canvas_->anims_.push_back(this);
        }

        if (screen_) {
            screen_->anims_.push_back(this);
        }
    }

    anim_executor::~anim_executor() {
        if (canvas_) {
            canvas_->remove_canvas_observer(this);

            auto ite = std::find(canvas_->anims_.begin(), canvas_->anims_.end(), this);
            if (ite != canvas_->anims_.end()) {
                canvas_->anims_.erase(ite);
            }
        }

        if (screen_) {
            auto ite = std::find(screen_->anims_.begin(), screen_->anims_.end(), this);
            if (ite != screen_->anims_.end()) {
                screen_->anims_.erase(ite);
            }
        }
    }

    void anim_executor::on_window_destroyed(canvas_interface *obj) {
        if (canvas_ == obj) {
            canvas_ = nullptr;
        }
    }

    void anim_executor::request_redraw(const bool full) {
        if (!canvas_ || !screen_) {
            return;
        }

        if (full) {
            screen_->flags_ |= screen::FLAG_SERVER_REDRAW_PENDING;
        }

        if (!canvas_->can_be_physically_seen()) {
            return;
        }

        window_server &serv = canvas_->client->get_ws();
        serv.get_anim_scheduler()->schedule_if_sooner(serv.get_graphics_driver(), screen_, serv.get_ntimer()->microseconds());
    }

    anim_dll::anim_dll(window_server_client_ptr client, screen *scr, anim_executor_factory *factory)
        : window_client_obj(client, scr)
        , factory_(factory) {
    }

    /**
     * The arguments of a CreateInstance, Command or CommandReply: the bytes that follow the fixed header of the
     * command in the buffer (the TPtrC8 aArgs of RAnim::Construct/Command/CommandReply). Constructor buffers too long
     * for the command buffer come through the remote-read slot instead.
     */
    // A command is the instance handle and the opcode, followed inline by its arguments (RAnim::Command/CommandReply's
    // aArgs). TWsAnimDllCmdCreateInstance is the window handle and the type; on EKA1 a pointer to the client's
    // descriptor holding RAnim::Construct's aArgs follows, which the server reads from the client (RMessage::ReadL),
    // and on EKA2 that descriptor is IPC slot 1 (CL_STD.H's KIpcSlot).
    static constexpr std::size_t ANIM_COMMAND_HEADER_SIZE = 8;
    static constexpr std::size_t ANIM_CREATE_INSTANCE_HEADER_SIZE = 8;

    static const std::uint8_t *anim_command_arguments(ws_cmd &cmd, std::size_t &size) {
        if (cmd.header.cmd_len > ANIM_COMMAND_HEADER_SIZE) {
            size = cmd.header.cmd_len - ANIM_COMMAND_HEADER_SIZE;
            return reinterpret_cast<const std::uint8_t *>(cmd.data_ptr) + ANIM_COMMAND_HEADER_SIZE;
        }

        size = 0;
        return nullptr;
    }

    static const std::uint8_t *anim_construct_arguments(service::ipc_context &ctx, ws_cmd &cmd, std::size_t &size) {
        size = 0;
        kernel::process *own_pr = ctx.msg->own_thr->owning_process();

        if (ctx.sys->get_kernel_system()->is_eka1()) {
            if (cmd.header.cmd_len < ANIM_CREATE_INSTANCE_HEADER_SIZE + sizeof(std::uint32_t)) {
                return nullptr;
            }

            std::uint32_t descriptor_address = 0;
            std::memcpy(&descriptor_address, reinterpret_cast<const std::uint8_t *>(cmd.data_ptr) + ANIM_CREATE_INSTANCE_HEADER_SIZE,
                sizeof(descriptor_address));

            epoc::des8 *descriptor = eka2l1::ptr<epoc::des8>(descriptor_address).get(own_pr);
            if (!descriptor) {
                return nullptr;
            }

            size = descriptor->get_length();
            return reinterpret_cast<const std::uint8_t *>(descriptor->get_pointer_raw(own_pr));
        }

        const std::size_t slot_size = ctx.get_argument_data_size(1);
        const std::uint8_t *slot_data = ctx.get_descriptor_argument_ptr(1);

        if (!slot_data || (slot_size == static_cast<std::size_t>(-1))) {
            return nullptr;
        }

        size = slot_size;
        return slot_data;
    }

    bool anim_dll::execute_command(service::ipc_context &ctx, ws_cmd &cmd) {
        ws_anim_dll_opcode op = static_cast<decltype(op)>(cmd.header.op);
        bool quit = false;

        if (anim_trace_enabled()) {
            anim_trace(cmd);
        }

        switch (op) {
        case ws_anim_dll_op_create_instance: {
            if (!factory_) {
                LOG_WARN(SERVICE_WINDOW, "No factory present to create instance of animation!");
                ctx.complete(epoc::error_none);
                break;
            }

            anim_create_instance_args *anim_args = reinterpret_cast<anim_create_instance_args *>(cmd.data_ptr);
            canvas_base *canvas = dynamic_cast<canvas_base *>(client->get_object(anim_args->win_handle_));

            if (canvas == nullptr) {
                ctx.complete(epoc::error_bad_handle);
                break;
            }

            auto executor = factory_->new_executor(canvas, anim_args->anim_type_);
            if (executor == nullptr) {
                LOG_TRACE(SERVICE_WINDOW, "Can't create animation from animation factory \"{}\"!", factory_->name());
                ctx.complete(epoc::error_general);

                break;
            }

            std::size_t args_size = 0;
            const std::uint8_t *args = anim_construct_arguments(ctx, cmd, args_size);

            if (anim_trace_enabled() && args) {
                LOG_INFO(SERVICE_WINDOW, "Anim DLL construct arguments ({} bytes): {}", args_size, anim_hex(args, args_size));
            }

            const std::int32_t construct_result = executor->construct(args, args_size);
            if (construct_result != epoc::error_none) {
                ctx.complete(construct_result);
                break;
            }

            ctx.complete(static_cast<int>(executors_.add(executor)));
            break;
        }

        case ws_anim_dll_op_command:
        case ws_anim_dll_op_command_reply: {
            if (!factory_) {
                ctx.complete(epoc::error_none);
                break;
            }

            anim_request_info *req_info = reinterpret_cast<anim_request_info *>(cmd.data_ptr);
            std::unique_ptr<anim_executor> *executor = executors_.get(req_info->handle_);

            if (!executor) {
                ctx.complete(epoc::error_bad_handle);
                break;
            }

            std::size_t args_size = 0;
            const std::uint8_t *args = anim_command_arguments(cmd, args_size);

            const std::int32_t result = (*executor)->handle_request(ctx, static_cast<std::int32_t>(req_info->opcode_), args, args_size);

            // A Command (no reply) still ends the buffer flush it came in, like every other buffered command.
            ctx.complete((op == ws_anim_dll_op_command_reply) ? result : epoc::error_none);
            break;
        }

        case ws_anim_dll_op_free: {
            ctx.complete(epoc::error_none);
            client->delete_object(cmd.obj_handle);

            quit = true;
            break;
        }

        case ws_anim_dll_op_destroy_instance: {
            if (!factory_) {
                ctx.complete(epoc::error_none);
                break;
            }
            executors_.remove(*reinterpret_cast<std::uint32_t *>(cmd.data_ptr));
            ctx.complete(epoc::error_none);
            break;
        }

        default: {
            LOG_ERROR(SERVICE_WINDOW, "Unimplemented AnimDll opcode: 0x{:x}", cmd.header.op);
            ctx.complete(epoc::error_none);
            break;
        }
        }

        return quit;
    }
}
