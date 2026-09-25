/*
 * Copyright (c) 2022 EKA2L1 Team
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

#pragma once

#include <kernel/server.h>
#include <services/framework.h>

#include <memory>

namespace eka2l1 {
    class backup_old_server : public service::typical_server {
    public:
        explicit backup_old_server(eka2l1::system *sys);
        void connect(service::ipc_context &context) override;
    };

    // BAFL's backup server (backup_std.h TBaBakOpCode, 7.0s numbering from 20).
    enum backup_old_opcode {
        backup_old_event_ready = 20,
        backup_old_get_event,
        backup_old_close_all_files,
        backup_old_restart_all,
        backup_old_close_file,
        backup_old_restart_file,
        backup_old_notify_lock_change,
        backup_old_notify_lock_change_cancel,
        backup_old_close_server,
        backup_old_notify_backup_operation,
        backup_old_cancel_outstanding_backup_operation_event,
        backup_old_get_backup_operation_state,
        backup_old_backup_operation_event_ready,
        backup_old_get_backup_operation_event,
        backup_old_set_backup_operation_observer_is_present,
        backup_old_stop_notifications
    };

    struct backup_old_session : public service::typical_session {
        // The two notification pulls stay pending until the server has something to report.
        // No backup or restore ever runs here, so they only ever end by cancel or stop.
        std::unique_ptr<service::ipc_context> event_ready_;
        std::unique_ptr<service::ipc_context> operation_event_ready_;
        bool operation_observer_present_ = false;

        explicit backup_old_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version);

        void fetch(service::ipc_context *ctx) override;
    };
}
