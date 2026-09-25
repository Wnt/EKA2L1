/*
 * Copyright (c) 2018 EKA2L1 Team
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

#include <common/log.h>
#include <utils/err.h>

#include <kernel/kernel.h>
#include <kernel/server.h>
#include <kernel/timing.h>

#include <config/config.h>

namespace eka2l1::service {
    server::~server() {
    }

    // Create a server with name
    server::server(kernel_system *kern, system *sys, kernel::thread *owner, const std::string name, bool hle, bool unhandle_callback_enable, const service::share_mode shmode)
        : kernel_obj(kern, name, nullptr, kernel::access_type::global_access)
        , sys(sys)
        , hle(hle)
        , owner_thread(owner)
        , unhandle_callback_enable(unhandle_callback_enable)
        , shmode_(shmode) {
        obj_type = kernel::object_type::server;

        if (owner_thread)
            owner_thread->increase_access_count();

        REGISTER_IPC(server, connect, -1, "Server::Connect");
        REGISTER_IPC(server, disconnect, -2, "Server::Disconnect");
    }

    void server::receive(ipc_msg_ptr &msg) {
        msg = nullptr;

        if (!delivered_msgs.empty()) {
            common::double_linked_queue_element *deliver_first = delivered_msgs.first();
            msg = E_LOFF(deliver_first, ipc_msg, delivered_msg_link);

            deliver_first->deque();
        }
    }

    bool server::ready() {
        return request_status && request_data;
    }

    int server::deliver(ipc_msg_ptr msg) {
        // Is ready
        if (ready()) {
            accept(msg, true);
        } else {
            msg->msg_status = ipc_message_status::delivered;
            delivered_msgs.push(&msg->delivered_msg_link);
        }

        return 0;
    }

    void server::register_ipc_func(uint32_t ordinal, ipc_func func) {
        ipc_funcs.emplace(ordinal, func);
    }

    void server::detach(session *svse) {
        auto ite = std::find(sessions.begin(), sessions.end(), svse);
        if (ite != sessions.end()) {
            sessions.erase(ite);
        }
    }

    int server::destroy() {
        if (owner_thread)
            owner_thread->decrease_access_count();

        for (std::size_t i = 0; i < sessions.size(); i++) {
            sessions[i]->detatch(epoc::error_server_terminated);

            if (!is_hle()) {
                // HLE may still need server pointer for deinit
                sessions[i]->svr = nullptr;
            }
        }

        sessions.clear();
        return 0;
    }

    void server::accept(ipc_msg_ptr msg, const bool notify_owner) {
        msg->msg_status = ipc_message_status::accepted;

        // On some platforms where IPCv1 is still relevant, odd pointer is used to indicates new IPCv2
        if (kern->is_ipc_old() || (kern->is_eka1() && !(request_data.ptr_address() & 1))) {
            message1 *dat_hle = request_data.cast<message1>().get(request_own_thread->owning_process());

            dat_hle->ipc_msg_handle = msg->id;
            dat_hle->function = msg->function;
            dat_hle->session_ptr = msg->session_ptr_lle;
            // EKA1 RMessage::Client() is the session's client: every message of one session carries the same RThread
            // handle for as long as the session lives, and servers match requests by it. The Series 80 SkinServer
            // keeps each client's op 6 notification with its client handle; op 7 (sent by the skin client's
            // CActive::Cancel) completes the notification with KErrCancel only when its client handle is the one
            // the notification came with, and closing another session of the same thread drops notifications of
            // that session's client handle. A fresh handle per message (closed on completion) meant op 7 matched
            // nothing, so every app's Exit parked in that Cancel for good (CEikonEnv::DestroyEnvironment ->
            // skin.dll -> CActive::Cancel -> User::WaitForRequest) and the app never ended.
            const std::uint64_t client_key = (static_cast<std::uint64_t>(request_own_thread->unique_id()) << 32)
                | msg->sender_session_uid;
            kernel::handle client_handle = kernel::INVALID_HANDLE;

            auto cached = eka1_client_handles_.find(client_key);
            if ((cached != eka1_client_handles_.end()) && (kern->get_kernel_obj_raw(cached->second, request_own_thread) == msg->own_thr)) {
                client_handle = cached->second;
            } else {
                client_handle = kern->open_handle_with_thread(request_own_thread, msg->own_thr, kernel::owner_type::thread);
                eka1_client_handles_[client_key] = client_handle;
            }

            dat_hle->client_thread_handle = client_handle;

            std::copy(msg->args.args, msg->args.args + 4, dat_hle->args);

            // The session ends with its disconnect message: its handle goes when that message is completed.
            if (msg->type == ipc_message_type_disconnect) {
                eka1_client_handles_.erase(client_key);
                msg->thread_handle_low = client_handle;
            } else {
                msg->thread_handle_low = 0;
            }
        } else {
            request_data = eka2l1::ptr<message2>(request_data.ptr_address() & ~1);
            message2 *dat_hle = request_data.get(request_own_thread->owning_process());

            dat_hle->ipc_msg_handle = msg->id;
            dat_hle->flags = msg->args.flag;
            dat_hle->function = msg->function;
            dat_hle->session_ptr = msg->session_ptr_lle;

            std::copy(msg->args.args, msg->args.args + 4, dat_hle->args);
            msg->thread_handle_low = 0;
        }

        (request_status.get(request_own_thread->owning_process()))->set(0, kern->is_eka1()); // KErrNone

        if (notify_owner) {
            request_own_thread->signal_request();
        }

        request_own_thread->decrease_access_count();

        request_own_thread = nullptr;
        request_status = 0;
        request_data = 0;
    }

    void server::receive_async_lle(eka2l1::ptr<epoc::request_status> msg_request_status,
        eka2l1::ptr<message2> data) {
        ipc_msg_ptr pending_msg = nullptr;
        receive(pending_msg);

        request_status = msg_request_status;
        request_own_thread = kern->crr_thread();
        request_data = data;

        request_own_thread->increase_access_count();

        if (!pending_msg) {
            return;
        }

        accept(pending_msg, true);
    }

    void server::cancel_async_lle() {
        if (!request_own_thread) {
            return;
        }

        if (!request_status) {
            request_own_thread->signal_request();
            return;
        }

        (request_status.get(request_own_thread->owning_process()))->set(-3, kern->is_eka1()); // KErrCancel
        request_own_thread->signal_request();
        request_own_thread->decrease_access_count();

        request_own_thread = nullptr;
        request_status = 0;
        request_data = 0;
    }

    // IPC process related functions belong in context.cpp
}
