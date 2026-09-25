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
#include <system/epoc.h>
#include <utils/err.h>

#include <services/ui/cap/oom_app.h>
#include <services/ui/eikappui.h>
#include <services/window/window.h>
#include <kernel/kernel.h>
#include <kernel/thread.h>

#include <cstring>

namespace eka2l1 {
    std::string debug_preferences::to_buf() {
        // Write a dummy version
        std::string buf;
        buf.resize(3);

        std::uint16_t eik_ver = 0;
        std::uint8_t flags8 = static_cast<std::uint8_t>(flags);

        std::memcpy(&buf[0], &eik_ver, sizeof(eik_ver));
        std::memcpy(&buf[sizeof(eik_ver)], &flags8, sizeof(flags8));

        return buf;
    }

    static const std::string get_eik_app_ui_server_name_by_epocver(const epocver the_ver) {
        if (the_ver < epocver::eka2) {
            return "EikAppUiServer";
        }

        return "!EikAppUiServer";
    }

    eikappui_server::eikappui_server(eka2l1::system *sys)
        : service::typical_server(sys, get_eik_app_ui_server_name_by_epocver(sys->get_symbian_version_use()))
        , cap_server_(nullptr) {
    }

    void eikappui_server::connect(service::ipc_context &ctx) {
        if (!cap_server_) {
            cap_server_ = reinterpret_cast<oom_ui_app_server *>(kern->get_by_name<service::server>(OOM_APP_UI_SERVER_NAME));
        }

        create_session<eikappui_session>(&ctx);
        typical_server::connect(ctx);
    }

    void eikappui_server::disconnect(service::ipc_context &ctx) {
        remove_session(ctx.msg->msg_session->unique_id());
        typical_server::disconnect(ctx);
    }

    // TODO: Make a resource reader and read from the config resource file
    void eikappui_server::get_debug_preferences(service::ipc_context &ctx) {
        debug_preferences preferences;
        LOG_TRACE(SERVICE_UI, "GetDebugPreferences stubbed");

        std::string buf = preferences.to_buf();

        // Buffer is not large enough. Unless the length is specified as 0, we are still doing this check
        if ((*ctx.get_argument_value<int>(0) != 0) && (static_cast<int>(buf.size()) > *ctx.get_argument_value<int>(0))) {
            ctx.complete(static_cast<int>(buf.size()));
            return;
        }

        ctx.write_data_to_descriptor_argument(1, reinterpret_cast<std::uint8_t *>(&buf[0]),
            static_cast<std::uint32_t>(buf.size()));

        ctx.complete(0);
    }

    eikappui_session::eikappui_session(service::typical_server *svr, kernel::uid client_ss_uid, epoc::version client_version)
        : service::typical_session(svr, client_ss_uid, client_version)
        , cap_session_(nullptr) {
        eikappui_server *parent = reinterpret_cast<eikappui_server *>(svr);
        cap_session_ = parent->cap_server_->create_session_impl<oom_ui_app_session>(client_ss_uid, client_version, true);
    }

    eikappui_session::~eikappui_session() {
        eikappui_server *serv = server<eikappui_server>();
        serv->cap_server_->remove_session(client_ss_uid_);
    }

    // Series 80 v2 EikSrv (CKON eiksrvs.h, TEikUiOpCode). Only 9/10 happen to line up with the
    // S60 table after the EKA1 remap below; 5, 6 and 13 would land on blank, unblank and the Avkon
    // SGC range. The task list, the back-stepping application stack and the status pane live in the
    // ROM's EikSrv, which this HLE server stands in for: they are accepted and have no effect here.
    enum eik_app_ui_s80_opcode {
        eik_s80_notify_alarm_server_of_task_change = 0,
        eik_s80_launch_task_list = 1,
        eik_s80_cycle_tasks = 2,
        eik_s80_add_to_stack = 3,
        eik_s80_remove_from_stack = 4,
        eik_s80_execute_d = 5,
        eik_s80_set_status_pane_flags = 6,
        eik_s80_set_status_pane_layout = 7,
        eik_s80_notifier = 8,
        eik_s80_blank_screen = 9,
        eik_s80_unblank_screen = 10,
        eik_s80_resolve_error = 11,
        eik_s80_extension = 12,
        eik_s80_update_status_pane_layout = 13
    };

    void eikappui_session::fetch_s80(service::ipc_context *ctx) {
        switch (ctx->msg->function) {
        case eik_s80_blank_screen:
            cap_session_->blank_screen(ctx);
            break;

        case eik_s80_unblank_screen:
            cap_session_->unblank_screen(ctx);
            break;

        case eik_s80_set_status_pane_layout: {
            // Argument 0 is the layout's BASESKIN resource id. The window server paints the pane in the
            // ROM Eikon server's place (services/window/s80pane.h), behind the application that sent it.
            const std::uint32_t layout = *ctx->get_argument_value<std::uint32_t>(0);
            window_server *winserv = reinterpret_cast<window_server *>(ctx->sys->get_kernel_system()->get_by_name<service::server>(
                eka2l1::get_winserv_name_by_epocver(ctx->sys->get_symbian_version_use())));

            if (winserv && ctx->msg->own_thr) {
                winserv->s80_set_status_pane_layout(ctx->msg->own_thr->owning_process(), layout);
            }

            LOG_TRACE(SERVICE_UI, "Series 80 EikSrv SetStatusPaneLayout 0x{:X}", layout);
            ctx->complete(epoc::error_none);
            break;
        }

        case eik_s80_notify_alarm_server_of_task_change:
        case eik_s80_launch_task_list:
        case eik_s80_cycle_tasks:
        case eik_s80_add_to_stack:
        case eik_s80_remove_from_stack:
        case eik_s80_set_status_pane_flags:
        case eik_s80_notifier:
        case eik_s80_extension:
        case eik_s80_update_status_pane_layout:
            LOG_TRACE(SERVICE_UI, "Series 80 EikSrv op {} accepted without effect", ctx->msg->function);
            ctx->complete(epoc::error_none);
            break;

        default:
            // ExecuteD (a dialog run by the server) and ResolveError need the ROM's EikSrv.
            LOG_WARN(SERVICE_UI, "Unimplemented Series 80 EikSrv opcode {}", ctx->msg->function);
            ctx->complete(epoc::error_none);
            break;
        }
    }

    void eikappui_session::fetch(service::ipc_context *ctx) {
        if (ctx->sys->get_symbian_version_use() == epocver::epoc7) {
            fetch_s80(ctx);
            return;
        }

        if (ctx->msg->function >= eik_app_ui_range_sgc) {
            // Redirect to cap session
            ctx->msg->function = ctx->msg->function - eik_app_ui_range_sgc + akn_eik_app_ui_set_sgc_params;
            cap_session_->fetch(ctx);

            return;
        } else {
            if (ctx->sys->get_symbian_version_use() <= epocver::eka2) {
                if (ctx->msg->function >= 9) {
                    ctx->msg->function -= (9 - eik_app_ui_blank_screen);
                }
            }
        }

        switch (ctx->msg->function) {
        case eik_app_ui_debug_prefs:
            server<eikappui_server>()->get_debug_preferences(*ctx);
            break;

        case eik_app_ui_blank_screen:
            cap_session_->blank_screen(ctx);
            break;

        case eik_app_ui_unblank_screen:
            cap_session_->unblank_screen(ctx);
            break;

        default:
            LOG_WARN(SERVICE_UI, "Unimplemented app ui session opcode 0x{:X}", ctx->msg->function);
            ctx->complete(epoc::error_none);
            break;
        }
    }
}
