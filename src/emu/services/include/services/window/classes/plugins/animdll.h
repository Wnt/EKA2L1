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

#include <cstdint>
#include <services/window/classes/wsobj.h>
#include <services/window/classes/winuser.h>
#include <common/container.h>
#include <common/vecx.h>

namespace eka2l1::epoc {
    struct screen;
    struct canvas_base;
    struct gdi_store_command_segment;

    struct anim_create_instance_args {
        std::uint32_t win_handle_;
        std::uint32_t anim_type_;
    };

    struct anim_request_info {
        std::uint32_t handle_;
        std::uint32_t opcode_;
    };

    /**
     * \brief The server side of one animation (a CAnim instance of an anim DLL).
     *
     * An executor is attached to the window it was created on. Executors that draw (the clock) do it the way a
     * CAnim does on the device: on top of the window's own content, clipped to what is visible of it, again after
     * every recomposition of the window and whenever their own state (the time shown) changes.
     */
    struct anim_executor : public canvas_observer {
        canvas_base *canvas_;
        screen *screen_;

    public:
        explicit anim_executor(canvas_base *canvas);
        virtual ~anim_executor();

        /**
         * \brief Receive the arguments of RAnim::Construct (CAnim::ConstructL's aArgs).
         * \returns An error code to complete the create request with, or zero.
         */
        virtual std::int32_t construct(const std::uint8_t *args, const std::size_t args_size) {
            return 0;
        }

        /**
         * \brief Handle RAnim::Command/CommandReply (CAnim::Command/CommandReplyL).
         *
         * On EKA1 the arguments follow the instance handle and the opcode inline in the command buffer, and a
         * command that answers with a structure passes a pointer to the client's own descriptor among them; on EKA2
         * such a descriptor is IPC slot 1 (the clock's KIpcSlot).
         *
         * \returns The reply.
         */
        virtual std::int32_t handle_request(service::ipc_context &ctx, const std::int32_t opcode, const std::uint8_t *args,
            const std::size_t args_size) = 0;

        /**
         * \brief Whether what this animation shows changed since it was last drawn.
         */
        virtual bool overlay_dirty() {
            return false;
        }

        /**
         * \brief Record the animation's drawing, in window coordinates, and the rectangle it covers.
         * \returns False if there is nothing to draw.
         */
        virtual bool build_overlay(gdi_store_command_segment &segment, eka2l1::rect &bounds) {
            return false;
        }

        /**
         * \brief Time until what this animation shows next changes by itself (a clock tick).
         * \returns False if it does not change by itself.
         */
        virtual bool next_overlay_update(std::uint64_t &delay_us) {
            return false;
        }

        void on_window_size_changed(canvas_interface *obj) override {}
        void on_window_destroyed(canvas_interface *obj) override;

        /**
         * \brief Ask the screen to recompose, drawing this animation again.
         * \param full Recompose every window from its redraw store (the animation moved, shrank or hid).
         */
        void request_redraw(const bool full);
    };

    struct anim_executor_factory {
    public:
        virtual std::unique_ptr<anim_executor> new_executor(canvas_base *canvas, const std::uint32_t anim_type) = 0;
        virtual std::string name() const = 0;
        virtual ~anim_executor_factory() = default;
    };

    struct anim_dll : public window_client_obj {
    protected:
        anim_executor_factory *factory_;
        common::identity_container<std::unique_ptr<anim_executor>> executors_;

    public:
        explicit anim_dll(window_server_client_ptr client, screen *scr, anim_executor_factory *factory);
        bool execute_command(service::ipc_context &context, ws_cmd &cmd) override;
    };
}
