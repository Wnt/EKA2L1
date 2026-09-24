/*
 * Copyright (c) 2026 EKA2L1 Team
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

#include <services/dos/dos.h>

#include <common/log.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <mem/ptr.h>
#include <system/epoc.h>
#include <utils/des.h>
#include <utils/err.h>

#include <cstring>
#include <sstream>

namespace eka2l1 {
    // The 2002 (EKA1) client passes every argument as a raw pointer. A "TInt&" result is a
    // TPckg<TInt> in the client (a modifiable descriptor of type ptr, length 4); the server
    // writes the packed value into it. Anything else gets written as the plain variable.
    static constexpr std::uint32_t DOS_MAX_PLAUSIBLE_RESULT_DES = 0x1000;

    // RDosExtension::CallFunction packages this before the parameter data (dossvrservices.h).
    struct dos_extension_par {
        std::int32_t func_;
        std::int32_t par_length_;
        std::int32_t auto_complete_;
    };

    static_assert(sizeof(dos_extension_par) == 12);

    static const char *dos_opcode_name(const int fn) {
        switch (fn) {
        case dos_create_sysutils_subsession: return "CreateSysUtilsSubSession";
        case dos_create_helper_subsession: return "CreateHelperSubSession";
        case dos_create_mtc_subsession: return "CreateMtcSubSession";
        case dos_create_selftest_subsession: return "CreateSelfTestSubSession";
        case dos_create_sae_subsession: return "CreateSaeSubSession";
        case dos_create_accessory_subsession: return "CreateAccessorySubSession";
        case dos_create_audio_subsession: return "CreateAudioSubSession";
        case dos_create_extension_subsession: return "CreateExtensionSubSession";
        case dos_create_event_rcv_subsession: return "CreateEventRcvSubSession";
        case dos_create_event_snd_subsession: return "CreateEventSndSubSession";
        case dos_create_btaudio_subsession: return "CreateBTAudioSubSession";
        case dos_create_shareddata_subsession: return "CreateSubSession(111)";
        case dos_close_subsession: return "CloseSubSession";
        case dos_get_sim_language: return "GetSimLanguage";
        case dos_perform_dos_rfs: return "PerformDosRfs";
        case dos_set_dos_alarm: return "SetDosAlarm";
        case dos_get_startup_reason: return "GetStartupReason";
        case dos_get_sw_startup_reason: return "GetSWStartupReason";
        case dos_set_sw_startup_reason: return "SetSWStartupReason";
        case dos_helper_hidden_reset: return "HiddenReset";
        case dos_get_rtc_status: return "GetRTCStatus";
        case dos_generate_grip_event: return "GenerateGripEvent";
        case dos_power_on: return "PowerOn";
        case dos_power_off: return "PowerOff";
        case dos_reset_generate: return "ResetGenerate";
        case dos_set_state: return "SetState";
        case dos_sync: return "DosSync";
        case dos_set_state_flag: return "SetStateFlag";
        case dos_get_state_flag: return "GetStateFlag";
        case dos_shutdown_sync: return "DosShutdownSync";
        case dos_perform_self_test: return "PerformSelfTest";
        case dos_start_sae: return "StartSae";
        case dos_call_function: return "CallFunction";
        case dos_register_event: return "RegisterEvent";
        case dos_unregister_event: return "UnRegisterEvent";
        case dos_wait_event: return "WaitEvent";
        case dos_event_firing: return "EventFiring";
        case dos_cancel_wait_event: return "CancelWaitEvent";
        case dos_server_shutdown: return "ServerShutdown";
        case dos_request_free_disk_space: return "RequestFreeDiskSpace";
        case dos_request_free_disk_space_cancel: return "RequestFreeDiskSpaceCancel";
        default:
            break;
        }

        if ((fn >= dos_accessory_first) && (fn <= dos_accessory_last)) {
            return "Accessory*";
        }

        if ((fn >= dos_audio_first) && (fn <= dos_audio_last)) {
            return "Audio*";
        }

        if ((fn >= dos_btaudio_first) && (fn <= dos_btaudio_last)) {
            return "BTAudio*";
        }

        return "?";
    }

    dos_server::dos_server(eka2l1::system *sys)
        : service::typical_server(sys, "DosServer") {
    }

    void dos_server::connect(service::ipc_context &context) {
        create_session<dos_session>(&context);
        context.complete(epoc::error_none);
    }

    void dos_server::raise_event(const std::uint32_t event, const std::uint8_t *param, const std::size_t param_len) {
        for (auto &[uid, session] : sessions) {
            reinterpret_cast<dos_session *>(session.get())->on_event(event, param, param_len);
        }
    }

    dos_session::dos_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version) {
    }

    dos_session::~dos_session() {
        for (auto &[handle, sub] : subsessions_) {
            if (sub.pending_wait_) {
                sub.pending_wait_->complete(epoc::error_cancel);
            }
        }
    }

    void dos_session::trace(service::ipc_context *ctx) {
        kernel::process *pr = ctx->msg->own_thr->owning_process();
        std::ostringstream args;

        for (int i = 0; i < 4; i++) {
            const std::uint32_t arg = static_cast<std::uint32_t>(ctx->msg->args.args[i]);
            args << fmt::format(" a{}=0x{:X}", i, arg);

            // A pointer into the client shows its first two words: a descriptor header
            // (type in the top nibble, length below) and the first data word.
            if (arg >= 0x1000) {
                std::uint32_t *words = eka2l1::ptr<std::uint32_t>(arg).get(pr);
                if (words) {
                    args << fmt::format("[{:08X} {:08X}]", words[0], words[1]);
                }
            }
        }

        LOG_INFO(SERVICE_HWRM, "[DosServer] {} ({}){} from {}", dos_opcode_name(ctx->msg->function),
            ctx->msg->function, args.str(), ctx->msg->own_thr->name());
    }

    bool dos_session::write_result(service::ipc_context *ctx, const int idx, const void *data, const std::size_t size) {
        kernel::process *pr = ctx->msg->own_thr->owning_process();
        const std::uint32_t arg = static_cast<std::uint32_t>(ctx->msg->args.args[idx]);

        epoc::des8 *des = eka2l1::ptr<epoc::des8>(arg).get(pr);

        if (!des) {
            LOG_WARN(SERVICE_HWRM, "[DosServer] result argument {} (0x{:X}) does not point into the client", idx, arg);
            return false;
        }

        const epoc::des_type type = des->get_descriptor_type();

        if ((type == epoc::ptr) || (type == epoc::buf) || (type == epoc::ptr_to_buf)) {
            const std::uint32_t max_len = des->get_max_length(pr);

            if ((max_len >= size) && (max_len <= DOS_MAX_PLAUSIBLE_RESULT_DES)) {
                return ctx->write_data_to_descriptor_argument(idx, reinterpret_cast<const std::uint8_t *>(data),
                    static_cast<std::uint32_t>(size));
            }
        }

        // Not a modifiable descriptor: the client handed over the variable itself.
        std::uint8_t *raw = eka2l1::ptr<std::uint8_t>(arg).get(pr);

        if (!raw) {
            return false;
        }

        std::memcpy(raw, data, size);
        LOG_INFO(SERVICE_HWRM, "[DosServer] wrote {} result bytes to raw client pointer 0x{:X}", size, arg);

        return true;
    }

    dos_subsession *dos_session::subsession_of(service::ipc_context *ctx) {
        // Subsession messages carry the handle in the last argument.
        const std::uint32_t handle = static_cast<std::uint32_t>(ctx->msg->args.args[3]);
        auto ite = subsessions_.find(handle);

        if (ite == subsessions_.end()) {
            return nullptr;
        }

        return &ite->second;
    }

    void dos_session::create_subsession(service::ipc_context *ctx, const std::int32_t kind) {
        const std::uint32_t handle = next_handle_++;
        subsessions_.emplace(handle, dos_subsession(kind));

        // RSubSessionBase::CreateSubSession passes the address of its handle package as
        // the last argument; the server answers by writing the new handle there.
        if (!write_result(ctx, 3, &handle, sizeof(handle))) {
            LOG_ERROR(SERVICE_HWRM, "[DosServer] can't hand the subsession handle back to the client");
            subsessions_.erase(handle);
            ctx->complete(epoc::error_bad_descriptor);
            return;
        }

        ctx->complete(epoc::error_none);
    }

    void dos_session::close_subsession(service::ipc_context *ctx) {
        const std::uint32_t handle = static_cast<std::uint32_t>(ctx->msg->args.args[3]);
        auto ite = subsessions_.find(handle);

        if (ite != subsessions_.end()) {
            if (ite->second.pending_wait_) {
                ite->second.pending_wait_->complete(epoc::error_cancel);
            }

            subsessions_.erase(ite);
        }

        ctx->complete(epoc::error_none);
    }

    void dos_session::call_function(service::ipc_context *ctx) {
        // arg0: TExtensionParPckg, arg1: parameter descriptor, arg2: autocomplete flag.
        std::optional<dos_extension_par> par = ctx->get_argument_data_from_descriptor<dos_extension_par>(0, true);
        std::ostringstream param_hex;

        if (par) {
            const std::uint8_t *param = ctx->get_descriptor_argument_ptr(1);
            const std::size_t param_len = param ? ctx->get_argument_data_size(1) : 0;

            for (std::size_t i = 0; (i < param_len) && (i < 32); i++) {
                param_hex << fmt::format("{:02X} ", param[i]);
            }

            LOG_INFO(SERVICE_HWRM, "[DosServer] extension function {} (param {} bytes, autocomplete {}): {}",
                par->func_, par->par_length_, par->auto_complete_, param_hex.str());
        } else {
            LOG_WARN(SERVICE_HWRM, "[DosServer] extension call without a readable parameter package");
        }

        // The phone plug-in is not here; the call succeeds and the parameter stays as the
        // caller filled it. Asynchronous calls are completed at once too.
        ctx->complete(epoc::error_none);
    }

    void dos_session::register_event(service::ipc_context *ctx) {
        dos_subsession *sub = subsession_of(ctx);

        if (!sub) {
            ctx->complete(epoc::error_bad_handle);
            return;
        }

        sub->event_ = static_cast<std::uint32_t>(ctx->msg->args.args[0]);
        sub->registered_ = true;

        LOG_INFO(SERVICE_HWRM, "[DosServer] {} listens to DOS event 0x{:X}", ctx->msg->own_thr->name(), sub->event_);
        ctx->complete(epoc::error_none);
    }

    void dos_session::deliver(dos_subsession &sub, const std::vector<std::uint8_t> &param) {
        if (!sub.pending_wait_) {
            return;
        }

        std::unique_ptr<service::ipc_context> waiter = std::move(sub.pending_wait_);

        if (!param.empty()) {
            waiter->write_data_to_descriptor_argument(1, param.data(), static_cast<std::uint32_t>(param.size()),
                nullptr, true);
        }

        waiter->complete(epoc::error_none);
    }

    void dos_session::wait_event(service::ipc_context *ctx) {
        // arg0: queue type, arg1: parameter descriptor, arg2: 0 (no autocomplete).
        dos_subsession *sub = subsession_of(ctx);

        if (!sub) {
            ctx->complete(epoc::error_bad_handle);
            return;
        }

        if (sub->pending_wait_) {
            LOG_WARN(SERVICE_HWRM, "[DosServer] a second WaitEvent on one receiver; the first one is dropped");
            sub->pending_wait_->complete(epoc::error_cancel);
        }

        sub->queue_type_ = ctx->msg->args.args[0];
        sub->pending_wait_ = ctx->move_to_new();

        if (!sub->queued_.empty()) {
            std::vector<std::uint8_t> param = std::move(sub->queued_.front());
            sub->queued_.pop_front();
            deliver(*sub, param);
        }
    }

    void dos_session::cancel_wait_event(service::ipc_context *ctx) {
        dos_subsession *sub = subsession_of(ctx);

        if (sub && sub->pending_wait_) {
            std::unique_ptr<service::ipc_context> waiter = std::move(sub->pending_wait_);
            waiter->complete(epoc::error_cancel);
        }

        ctx->complete(epoc::error_none);
    }

    void dos_session::fire_event(service::ipc_context *ctx) {
        // RDosEventSnd: arg0 event, arg1 parameter pointer, arg2 parameter length.
        const std::uint32_t event = static_cast<std::uint32_t>(ctx->msg->args.args[0]);
        const std::int32_t len = ctx->msg->args.args[2];
        std::vector<std::uint8_t> param;

        if ((len > 0) && (len <= 0x1000)) {
            kernel::process *pr = ctx->msg->own_thr->owning_process();
            std::uint8_t *data = eka2l1::ptr<std::uint8_t>(static_cast<std::uint32_t>(ctx->msg->args.args[1])).get(pr);

            if (data) {
                param.assign(data, data + len);
            }
        }

        LOG_INFO(SERVICE_HWRM, "[DosServer] {} fires DOS event 0x{:X} ({} bytes)", ctx->msg->own_thr->name(), event, len);

        server<dos_server>()->raise_event(event, param.data(), param.size());
        ctx->complete(epoc::error_none);
    }

    void dos_session::on_event(const std::uint32_t event, const std::uint8_t *param, const std::size_t param_len) {
        std::vector<std::uint8_t> data(param, param + param_len);

        for (auto &[handle, sub] : subsessions_) {
            if (!sub.registered_ || (sub.event_ != event)) {
                continue;
            }

            if (sub.pending_wait_) {
                deliver(sub, data);
            } else if (sub.queue_type_ == 1) {
                // EQueue
                sub.queued_.push_back(data);
            } else if (sub.queue_type_ == 2) {
                // EOnlyLast
                sub.queued_.clear();
                sub.queued_.push_back(data);
            }
        }
    }

    void dos_session::fetch(service::ipc_context *ctx) {
        const int fn = ctx->msg->function;
        trace(ctx);

        if ((fn >= dos_create_subsession_first) && (fn <= dos_create_subsession_last)) {
            create_subsession(ctx, fn);
            return;
        }

        switch (fn) {
        case dos_close_subsession:
            close_subsession(ctx);
            return;

        // SysUtils
        case dos_get_sim_language: {
            const std::int32_t lang = dos_sim_language_english;
            write_result(ctx, 0, &lang, sizeof(lang));
            ctx->complete(epoc::error_none);
            return;
        }

        // Helper: the start-up questions Starter asks through SysUtil.
        case dos_get_startup_reason: {
            const std::int32_t reason = dos_startup_reason_normal;
            write_result(ctx, 0, &reason, sizeof(reason));
            ctx->complete(epoc::error_none);
            return;
        }

        case dos_get_sw_startup_reason: {
            const std::int16_t reason = dos_sw_startup_reason_normal;
            write_result(ctx, 0, &reason, sizeof(reason));
            ctx->complete(epoc::error_none);
            return;
        }

        case dos_helper_hidden_reset:
            // The answer is the completion code: EFalse.
            ctx->complete(dos_hidden_reset_false);
            return;

        case dos_get_rtc_status: {
            const std::int32_t valid = dos_rtc_status_valid;
            write_result(ctx, 0, &valid, sizeof(valid));
            ctx->complete(epoc::error_none);
            return;
        }

        case dos_get_state_flag: {
            const std::int32_t flag = dos_offline_state_flag_false;
            write_result(ctx, 0, &flag, sizeof(flag));
            ctx->complete(epoc::error_none);
            return;
        }

        case dos_call_function:
            call_function(ctx);
            return;

        case dos_register_event:
            register_event(ctx);
            return;

        case dos_wait_event:
            wait_event(ctx);
            return;

        case dos_cancel_wait_event:
            cancel_wait_event(ctx);
            return;

        case dos_event_firing:
            fire_event(ctx);
            return;

        case dos_reset_generate:
        case dos_power_off:
            LOG_WARN(SERVICE_HWRM, "[DosServer] {} asked the phone to {} -- ignored, the emulator keeps running",
                ctx->msg->own_thr->name(), dos_opcode_name(fn));
            ctx->complete(epoc::error_none);
            return;

        // Everything else is a fire-and-forget order to hardware that is not here:
        // SetDosAlarm, PerformDosRfs, SetSWStartupReason, GenerateGripEvent, PowerOn,
        // SetState, DosSync, SetStateFlag, DosShutdownSync, PerformSelfTest, StartSae,
        // UnRegisterEvent, ServerShutdown, accessory/audio, free-disk-space watch.
        default:
            if (dos_opcode_name(fn)[0] == '?') {
                LOG_ERROR(SERVICE_HWRM, "[DosServer] unknown opcode {} from {}", fn, ctx->msg->own_thr->name());
            }

            ctx->complete(epoc::error_none);
            return;
        }
    }
}
