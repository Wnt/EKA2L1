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

#pragma once

#include <services/framework.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace eka2l1 {
    /**
     * \brief HLE stand-in for Nokia's Domestic OS server ("DosServer").
     *
     * On Nokia's Symbian OS 7.0s phones (Series 80 v2: Nokia 9300 / 9300i / 9500) the
     * ROM's boot chain (Starter.exe via SysUtil, SysAp.app, Startup.app, ...) talks to
     * DosServer.exe, whose plug-in (Nokia.dsy) speaks ISI to the cellular modem over the
     * ISA/XBUS link. The emulator has no phone side, so this server answers the same
     * client protocol the way Nokia's own phone-less SDK emulator plug-in (ExampleDSY)
     * does: a normal power-key start-up, no hidden reset, a valid RTC, SIM ok.
     *
     * Protocol: the EPL-released sf/os/devicesrv/dosservices/dosserver
     * (inc/dosclientserver.h, src/doscli*.cpp) and sf/adaptation/stubs/systemswstubs/
     * exampledsy. Nokia kept the opcode numbering from the 2002 server this ROM ships;
     * the numbers below are verified against the ROM's DSClient by IPC trace.
     */
    enum dos_opcode {
        // Subsession management
        dos_create_sysutils_subsession = 100,
        dos_create_helper_subsession = 101,
        dos_create_mtc_subsession = 102,
        dos_create_selftest_subsession = 103,
        dos_create_sae_subsession = 104,
        dos_create_accessory_subsession = 105,
        dos_create_audio_subsession = 106,
        dos_create_extension_subsession = 107,
        dos_create_event_rcv_subsession = 108,
        dos_create_event_snd_subsession = 109,
        dos_create_btaudio_subsession = 110,
        dos_create_shareddata_subsession = 111,
        // Symbian^3 numbers Close 111 and SharedData 112; the 2002 server this ROM ships
        // closes with 112 (X1's IPC trace of Nokia's SDK emulator: SUBCLOSE op=112). Every
        // opcode from 100 to 111 opens a subsession; the kind only matters for events, and
        // those identify themselves by the 9xx ops they send afterwards.
        dos_close_subsession = 112,
        dos_create_subsession_first = 100,
        dos_create_subsession_last = 111,

        // SysUtils
        dos_get_sim_language = 200,
        dos_perform_dos_rfs = 201,
        dos_set_dos_alarm = 202,

        // Helper
        dos_get_startup_reason = 300,
        dos_get_sw_startup_reason = 301,
        dos_set_sw_startup_reason = 302,
        dos_helper_hidden_reset = 303,
        dos_get_rtc_status = 304,
        dos_generate_grip_event = 305,

        // MTC
        dos_power_on = 400,
        dos_power_off = 401,
        dos_reset_generate = 402,
        dos_set_state = 403,
        dos_sync = 404,
        dos_set_state_flag = 405,
        dos_get_state_flag = 406,
        dos_shutdown_sync = 407,

        // SelfTest
        dos_perform_self_test = 500,

        // Sae
        dos_start_sae = 700,

        // Extension
        dos_call_function = 800,

        // Events
        dos_register_event = 900,
        dos_unregister_event = 901,
        dos_wait_event = 902,
        dos_event_firing = 903,
        dos_cancel_wait_event = 904,

        dos_server_shutdown = 1000,

        // Accessory
        dos_accessory_first = 1100,
        dos_accessory_last = 1105,

        // Audio
        dos_audio_first = 1200,
        dos_audio_last = 1218,

        // BT audio
        dos_btaudio_first = 1300,
        dos_btaudio_last = 1313,

        // SharedData
        dos_request_free_disk_space = 1400,
        dos_request_free_disk_space_cancel = 1401
    };

    // Answers, as ExampleDSY gives them (dsyhelper.cpp, dsysysutils.cpp): the values a
    // successful phone-less boot of Nokia's own SDK emulator logged as
    // "Startup reason query 0: 0,100,hid=0,rtc=1".
    enum dos_answer {
        dos_startup_reason_normal = 0,
        dos_sw_startup_reason_normal = 100,
        dos_hidden_reset_false = 0,
        dos_rtc_status_valid = 1,
        dos_sim_language_english = 1,
        dos_offline_state_flag_false = 0
    };

    struct dos_subsession {
        std::int32_t kind_;
        std::uint32_t event_ = 0;
        std::int32_t queue_type_ = 0;
        bool registered_ = false;

        std::unique_ptr<service::ipc_context> pending_wait_;
        std::deque<std::vector<std::uint8_t>> queued_;

        explicit dos_subsession(const std::int32_t kind)
            : kind_(kind) {
        }
    };

    class dos_server : public service::typical_server {
    public:
        explicit dos_server(eka2l1::system *sys);

        void connect(service::ipc_context &context) override;

        /**
         * \brief Deliver a DOS event to every registered receiver in every session.
         */
        void raise_event(const std::uint32_t event, const std::uint8_t *param, const std::size_t param_len);
    };

    struct dos_session : public service::typical_session {
    private:
        std::map<std::uint32_t, dos_subsession> subsessions_;
        std::uint32_t next_handle_ = 1;

        void trace(service::ipc_context *ctx);
        bool write_result(service::ipc_context *ctx, const int idx, const void *data, const std::size_t size);

        dos_subsession *subsession_of(service::ipc_context *ctx);
        void create_subsession(service::ipc_context *ctx, const std::int32_t kind);
        void close_subsession(service::ipc_context *ctx);

        void call_function(service::ipc_context *ctx);
        void register_event(service::ipc_context *ctx);
        void wait_event(service::ipc_context *ctx);
        void cancel_wait_event(service::ipc_context *ctx);
        void fire_event(service::ipc_context *ctx);

        void deliver(dos_subsession &sub, const std::vector<std::uint8_t> &param);

    public:
        explicit dos_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version);
        ~dos_session() override;

        void on_event(const std::uint32_t event, const std::uint8_t *param, const std::size_t param_len);
        void fetch(service::ipc_context *ctx) override;
    };
}
