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

#include <services/backup/backup.h>
#include <system/epoc.h>
#include <utils/err.h>

namespace eka2l1 {
    backup_old_server::backup_old_server(eka2l1::system *sys)
        : service::typical_server(sys, "BackupServer") {
    }

    void backup_old_server::connect(service::ipc_context &context) {
        create_session<backup_old_session>(&context);
        context.complete(epoc::error_none);
    }

    backup_old_session::backup_old_session(service::typical_server *serv, const kernel::uid ss_id,
        epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version) {
    }

    void backup_old_session::fetch(service::ipc_context *ctx) {
        // Semantics from CBaServBackupSession (Baksrv.cpp): everything but the two pulls completes
        // at once. With no backup running there is never an event, file lock change or closed file.
        switch (ctx->msg->function) {
        case backup_old_event_ready:
            event_ready_ = ctx->move_to_new();
            break;

        case backup_old_stop_notifications:
            if (event_ready_) {
                event_ready_->complete(epoc::error_none);
                event_ready_.reset();
            }

            ctx->complete(epoc::error_none);
            break;

        case backup_old_backup_operation_event_ready:
            if (operation_observer_present_) {
                operation_event_ready_ = ctx->move_to_new();
            } else {
                ctx->complete(epoc::error_none);
            }

            break;

        case backup_old_cancel_outstanding_backup_operation_event:
            operation_observer_present_ = false;

            if (operation_event_ready_) {
                operation_event_ready_->complete(epoc::error_cancel);
                operation_event_ready_.reset();
            }

            ctx->complete(epoc::error_none);
            break;

        case backup_old_set_backup_operation_observer_is_present:
            operation_observer_present_ = (ctx->get_argument_value<std::int32_t>(0).value_or(0) != 0);
            ctx->complete(epoc::error_none);
            break;

        case backup_old_get_backup_operation_event: {
            // TBackupOperationAttributes { TFileLockFlags iFileFlag; TOperationType iOperation }: ENone.
            const std::uint32_t attributes[2] = { 0, 0 };
            ctx->write_data_to_descriptor_argument(0, reinterpret_cast<const std::uint8_t *>(attributes), sizeof(attributes));
            ctx->complete(epoc::error_none);
            break;
        }

        case backup_old_get_event:
            // Only valid after EventReady completed, which never happens here.
            LOG_WARN(SERVICE_BACKUP, "GetEvent without a pending backup event");
            ctx->complete(epoc::error_not_found);
            break;

        case backup_old_get_backup_operation_state: {
            // TPckgC<TBool>: no backup operation is running.
            const std::int32_t running = 0;
            ctx->write_data_to_descriptor_argument<std::int32_t>(0, running);
            ctx->complete(epoc::error_none);
            break;
        }

        case backup_old_close_all_files:
        case backup_old_restart_all:
        case backup_old_close_file:
        case backup_old_restart_file:
        case backup_old_notify_lock_change:
        case backup_old_notify_lock_change_cancel:
        case backup_old_notify_backup_operation:
            // Registrations and requests that change nothing while no backup is running.
            ctx->complete(epoc::error_none);
            break;

        default:
            LOG_ERROR(SERVICE_BACKUP, "Unimplemented opcode for Old Backup server 0x{:X}", ctx->msg->function);
            ctx->complete(epoc::error_not_supported);
            break;
        }
    }
}
