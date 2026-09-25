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

#include <common/algorithm.h>
#include <common/log.h>
#include <common/time.h>
#include <common/cvt.h>
#include <common/path.h>
#include <common/platform.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <memory>
#include <unordered_map>

#include <services/accessory/accessory.h>
#include <services/alarm/alarm.h>
#include <services/applist/applist.h>
#include <services/audio/alf/alf.h>
#include <services/audio/keysound/keysound.h>
#include <services/audio/mmf/audio.h>
#include <services/audio/mmf/dev.h>
#include <services/backup/backup.h>
#include <services/bluetooth/bt.h>
#include <services/bluetooth/btman.h>
#include <services/camera/camera.h>
#include <services/centralrepo/centralrepo.h>
#include <services/comm/comm.h>
#include <services/domain/domain.h>
#include <services/dos/dos.h>
#include <services/drm/helper.h>
#include <services/drm/notifier/notifier.h>
#include <services/drm/rights/rights.h>
#include <services/etel/etel.h>
#include <services/fbs/fbs.h>
#include <services/featmgr/featmgr.h>
#include <services/fs/fs.h>
#include <services/goommonitor/goommonitor.h>
#include <services/hwrm/hwrm.h>
#include <services/internet/accesspoints.h>
#include <services/internet/connmonitor.h>
#include <services/internet/nifman.h>
#include <services/loader/loader.h>
#include <services/msv/msv.h>
#include <services/notifier/notifier.h>
#include <services/posix/posix.h>
#include <services/redir/redir.h>
#include <services/remcon/remcon.h>
#include <services/sensor/sensor.h>
#include <services/memorymanager/memorymanager.h>
#include <services/linnea/linnea.h>
#include <services/shutdown/shutdown.h>
#include <services/sisregistry/sisregistry.h>
#include <services/sms/settings.h>
#include <services/sms/sa/sa.h>
#include <services/sms/sendas/sendas.h>
#include <services/socket/server.h>
#include <services/sysagt/sysagt.h>
#include <services/timezone/timezone.h>
#include <services/ui/cap/oom_app.h>
#include <services/ui/eikappui.h>
#include <services/ui/icon/icon.h>
#include <services/ui/skin/server.h>
#include <services/ui/view/view.h>
#include <services/uiss/uiss.h>
#include <services/unipertar/unipertar.h>
#include <services/window/window.h>
#include <services/host_launch.h>

#include <services/init.h>
#include <system/epoc.h>
#include <vfs/vfs.h>
#include <utils/locale.h>
#include <utils/system.h>

#include <config/config.h>
#include <system/devices.h>

#if EKA2L1_PLATFORM(WIN32)
#include <Windows.h>
#endif

#define CREATE_SERVER_D(sys, svr, ...)                                                 \
    std::unique_ptr<service::server> temp = std::make_unique<svr>(sys, ##__VA_ARGS__); \
    sys->get_kernel_system()->add_custom_server(temp)

#define CREATE_SERVER(sys, svr, ...)                  \
    temp = std::make_unique<svr>(sys, ##__VA_ARGS__); \
    sys->get_kernel_system()->add_custom_server(temp)

#define DEFINE_INT_PROP_D(sys, category, key, data)                            \
    property_ptr prop = sys->get_kernel_system()->create<service::property>(); \
    prop->first = category;                                                    \
    prop->second = key;                                                        \
    prop->define(service::property_type::int_data, 0);                         \
    prop->set_int(data);

#define DEFINE_INT_PROP(sys, category, key, data)                 \
    prop = sys->get_kernel_system()->create<service::property>(); \
    prop->first = category;                                       \
    prop->second = key;                                           \
    prop->define(service::property_type::int_data, 0);            \
    prop->set_int(data);

#define DEFINE_BIN_PROP_D(sys, category, key, size, data)                      \
    property_ptr prop = sys->get_kernel_system()->create<service::property>(); \
    prop->first = category;                                                    \
    prop->second = key;                                                        \
    prop->define(service::property_type::bin_data, size);                      \
    prop->set(data);

#define DEFINE_BIN_PROP(sys, category, key, size, data)           \
    prop = sys->get_kernel_system()->create<service::property>(); \
    prop->first = category;                                       \
    prop->second = key;                                           \
    prop->define(service::property_type::bin_data, size);         \
    prop->set(data);

namespace eka2l1::epoc {
    epoc::locale get_locale_info() {
        epoc::locale locale;

        // TODO: Move to common
#if EKA2L1_PLATFORM(WIN32)
        locale.country_code_ = static_cast<int>(GetProfileInt("intl", "iCountry", 0));
#endif

        // TODO: These are stubbed!
        // See in relation: CLocale::MonetaryLoadLocaleL in ossrv, openenvcore's libc in file localeinfo.cpp
        locale.clock_format_ = epoc::clock_digital;
        locale.start_of_week_ = epoc::monday;
        locale.date_format_ = epoc::date_format_america;
        locale.time_format_ = epoc::time_format_twenty_four_hours;
        locale.universal_time_offset_ = -14400;
        locale.device_time_state_ = epoc::device_user_time;
        locale.decimal_separator_ = '.';
        locale.thousands_separator_ = ',';
        locale.negative_currency_format_ = epoc::negative_currency_leading_minus_sign;

        locale.time_separator_[0] = 0;
        locale.time_separator_[1] = ':';
        locale.time_separator_[2] = ':';
        locale.time_separator_[3] = 0;

        locale.date_separator_[0] = 0;
        locale.date_separator_[1] = '/';
        locale.date_separator_[2] = '/';
        locale.date_separator_[3] = 0;

        return locale;
    }

    // The Series 80 default locale: the one Nokia's Series 80 SDK ships in C:\System\Data\LOCALE.D00
    // (Finland: country 358, European day-month-year order, date separator '/', time ':', Monday
    // first, Mon-Fri working days, European summer-time zone), with a 24-hour clock and the host's
    // time zone (TZ) as the home zone. The ROM carries no locale file of its own, and the SDK file
    // is a better starting point for a Nordic device than the American stub above.
    static epoc::locale get_s80_default_locale() {
        epoc::locale locale{};

        const common::local_time_zone_info tz = common::get_local_time_zone_info(std::time(nullptr));

        locale.country_code_ = 358;
        locale.universal_time_offset_ = tz.offset_seconds - (tz.daylight_saving ? 3600 : 0);
        locale.date_format_ = epoc::date_format_european;
        locale.time_format_ = epoc::time_format_twenty_four_hours;
        locale.currency_symbol_position_ = epoc::locale_before;
        locale.currency_space_between_ = 1;
        locale.currency_decimal_places = 2;
        locale.negative_currency_format_ = epoc::negative_currency_leading_minus_sign;
        locale.currency_triads_allowed_ = 1;
        locale.thousands_separator_ = ',';
        locale.decimal_separator_ = '.';
        locale.date_separator_[1] = '/';
        locale.date_separator_[2] = '/';
        locale.time_separator_[1] = ':';
        locale.time_separator_[2] = ':';
        locale.am_pm_symbol_position_ = epoc::locale_after;
        locale.am_pm_space_between_ = 1;
        locale.home_daylight_saving_zone_ = epoc::daylight_saving_zone_european;
        locale.daylight_saving_ = tz.daylight_saving ? epoc::daylight_saving_zone_european : epoc::daylight_saving_zone_none;
        locale.work_days_ = 0x1F;
        locale.start_of_week_ = epoc::monday;
        locale.clock_format_ = epoc::clock_digital;
        locale.language_downgrades_[0] = 0xFFFF;
        locale.language_downgrades_[1] = 0xFFFF;
        locale.language_downgrades_[2] = 0xFFFF;
        locale.digit_type_ = epoc::digit_type_western;
        locale.device_time_state_ = epoc::device_user_time;

        return locale;
    }

    // BaflUtils::InitialiseLocale on Symbian 7.0s: the TLocale the user last set is kept, raw, in
    // C:\System\Data\LOCALE.D<nn> (BaflUtils::PersistLocale writes it right after TLocale::Set:
    // Control panel > Regional settings, or Clock > Change city through the world server). The
    // Nokia 9300 ROM writes LOCALE.D00 (seen: after Change city); the SDK emulator's C: also holds
    // a LOCALE.D01. Nothing in the ROM reads it back at boot under the emulator, so load it here,
    // over the default: LOCALE.D<language> first, then LOCALE.D00. Only the TLocale part is taken;
    // the file may be longer (the 7.0s one is 280 bytes).
    static bool load_persisted_locale(eka2l1::system *sys, const epoc::language lang, epoc::locale &locale) {
        io_system *io = sys->get_io_system();
        if (!io) {
            return false;
        }

        // The fields up to iDeviceTimeState are what 7.0s keeps; the rest is spare.
        static constexpr std::size_t LOCALE_FILE_MIN_SIZE = offsetof(epoc::locale, spare_);

        const int candidates[] = { static_cast<int>(lang), 0 };
        for (std::size_t i = 0; i < std::size(candidates); i++) {
            if ((i > 0) && (candidates[i] == candidates[0])) {
                continue;
            }

            const std::string name = fmt::format("C:\\System\\Data\\LOCALE.D{:02d}", candidates[i]);
            symfile f = io->open_file(common::utf8_to_ucs2(name), READ_MODE | BIN_MODE);
            if (!f) {
                continue;
            }

            epoc::locale loaded = locale;
            const std::size_t want = std::min<std::size_t>(sizeof(epoc::locale), static_cast<std::size_t>(f->size()));
            if ((want < LOCALE_FILE_MIN_SIZE) || (f->read_file(&loaded, 1, static_cast<std::uint32_t>(want)) != want)) {
                LOG_WARN(KERNEL, "{} is {} bytes, too short for a TLocale; ignored", name, f->size());
                continue;
            }

            locale = loaded;
            LOG_INFO(KERNEL, "Locale from {}: country {}, UTC offset {} s, summer time {}, date format {}, time format {}",
                name, locale.country_code_, locale.universal_time_offset_,
                epoc::locale_home_on_summer_time(locale) ? "on" : "off",
                static_cast<int>(locale.date_format_), static_cast<int>(locale.time_format_));
            return true;
        }

        return false;
    }

    // At boot, once the drives are mounted (initialize_system_properties runs before that): the
    // persisted TLocale, if the guest saved one, replaces the Series 80 default.
    static void load_s80_persisted_locale(eka2l1::system *sys) {
        kernel_system *kern = sys->get_kernel_system();
        if (!kern || !kern->is_eka1() || !sys->is_s80_device_active()) {
            return;
        }

        property_ptr prop = kern->get_prop(epoc::SYS_CATEGORY, epoc::LOCALE_DATA_KEY);
        std::optional<epoc::locale> current = prop ? prop->get_pkg<epoc::locale>() : std::nullopt;
        if (!current) {
            return;
        }

        epoc::locale locale = current.value();
        if (!load_persisted_locale(sys, static_cast<epoc::language>(kern->get_current_language()), locale)) {
            return;
        }

        // The file keeps the summer-time bits of the day it was saved. The device's world server
        // flips them at the change-over; under the emulator it only runs once an app starts it.
        // When the saved home zone is the host's zone (TZ), take today's state from the host, so
        // a golden saved in summer is right in winter.
        const common::local_time_zone_info tz = common::get_local_time_zone_info(std::time(nullptr));
        const std::int32_t host_zone = tz.offset_seconds - (tz.daylight_saving ? 3600 : 0);
        const std::uint32_t home = locale.home_daylight_saving_zone_ | epoc::daylight_saving_zone_dst_home;

        if ((locale.universal_time_offset_ == host_zone) && (locale.home_daylight_saving_zone_ != epoc::daylight_saving_zone_none)) {
            locale.daylight_saving_ = tz.daylight_saving ? (locale.daylight_saving_ | locale.home_daylight_saving_zone_)
                                                         : (locale.daylight_saving_ & ~home);
        }

        prop->set<epoc::locale>(locale);
        kern->set_utc_offset(epoc::locale_effective_utc_offset(locale));
    }

    static void initialize_system_properties(eka2l1::system *sys, eka2l1::config::state *cfg) {
        auto lang = epoc::locale_language{ epoc::lang_english, 0, 0, 0, 0, 0, 0, 0 };
        auto locale = epoc::get_locale_info();
        auto &dvcs = sys->get_device_manager()->get_devices();
        kernel_system *kern = sys->get_kernel_system();

        if (dvcs.size() > cfg->device) {
            auto &dvc = dvcs[cfg->device];

            if (cfg->language == -1) {
                lang.language = static_cast<epoc::language>(dvc.default_language_code);
            } else {
                lang.language = static_cast<epoc::language>(cfg->language);
            }
        }

        if (kern->is_eka1() && sys->is_s80_device_active()) {
            locale = get_s80_default_locale();
            // EKA1 home time follows the TLocale (see locale_set_eka1).
            kern->set_utc_offset(epoc::locale_effective_utc_offset(locale));
            kern->set_home_time_follows_locale(true);
        }

        address am_pm_names_addr[] = {
            kern->put_global_kernel_string("am"),
            kern->put_global_kernel_string("pm"),
        };

        lang.am_pm_table = eka2l1::ptr<char>(kern->put_static_array(am_pm_names_addr, 2));

        epoc::locale_locale_settings locale_settings;
        locale_settings.locale_extra_settings_dll_ptr = 0;
        locale_settings.currency_symbols[0] = '$';
        locale_settings.currency_symbols[1] = '\0';

        // Unknown key, testing show that this prop return 65535 most of times
        // The prop belongs to HAL server, but the key usuage is unknown. (TODO)
        DEFINE_INT_PROP_D(sys, epoc::SYS_CATEGORY, epoc::UNK_KEY1, 65535);
        DEFINE_INT_PROP(sys, epoc::SYS_CATEGORY, epoc::PHONE_POWER_KEY, system_agent_state_on);

        if (kern->is_eka1() && sys->is_s80_device_active()) {
            // System Agent states the Series 80 shell reads at boot (sacls.h). On a phone the
            // DOS server's plug-in publishes them from the modem; without one they are
            // KErrNotFound, and SysAp/Startup treat that as a fault. SIM present and ok,
            // no network, no charger, battery full, no call, ports idle, boxes empty.
            static constexpr std::pair<std::uint32_t, std::int32_t> S80_SYSTEM_AGENT_STATES[] = {
                { 0x100052C6, 0 }, // KUidSIMStatus = ESASimOk
                { 0x100052C7, 1 }, // KUidNetworkStatus = ESANetworkUnAvailable
                { 0x100052C8, 0 }, // KUidNetworkStrength = ESANetworkStrengthNone
                { 0x100052C9, 1 }, // KUidChargerStatus = ESAChargerDisconnected
                { 0x100052CA, 2 }, // KUidBatteryStrength = ESABatteryFull
                { 0x100052CB, 0 }, // KUidCurrentCall = ESACallNone
                { 0x100052CC, 0 }, // KUidDataPort = ESADataPortIdle
                { 0x100052CD, 0 }, // KUidInboxStatus = ESAInboxEmpty
                { 0x100052CE, 0 }, // KUidOutboxStatus = ESAOutboxEmpty
                { 0x100052D0, 0 }, // KUidAlarm = ESAAlarmOff
                // States the ROM's EikSrvUi and the SDK emulator's EikSrv ask for at start-up
                // (IPC trace) that no public header names. 0 is the "nothing special" value of
                // every System Agent state enum above.
                { 0x100052E9, 0 },
                { 0x1000A97F, 0 },
                { 0x101F8ED7, 0 },
            };

            for (const auto &[key, value] : S80_SYSTEM_AGENT_STATES) {
                if (!kern->get_prop(epoc::SYS_CATEGORY, key)) {
                    DEFINE_INT_PROP(sys, epoc::SYS_CATEGORY, key, value);
                }
            }
        }
        DEFINE_INT_PROP(sys, epoc::SYS_CATEGORY, epoc::SOFTWARE_INSTALL_KEY, 0);
        DEFINE_INT_PROP(sys, epoc::SYS_CATEGORY, epoc::SOFTWARE_LASTEST_UID_INSTALLATION, 0);

        // Published by the secure backup engine on a real device. Clients that watch the
        // backup state (File manager's backup engine for one) read it while constructing and
        // leave with KErrNotFound if it was never defined, taking the whole app down.
        DEFINE_INT_PROP(sys, epoc::SYS_CATEGORY, epoc::BACKUP_RESTORE_KEY, epoc::BACKUP_RESTORE_NORMAL_STATE);

        // From Domain Server request
        DEFINE_INT_PROP(sys, 0x1020e406, 0x250, 0);

        // Without these SysUtil's critical-disk-space check finds no threshold at all
        // and leaves, so a caller that checks free space before writing -- Camera
        // saving a photo, for one -- never gets to the write.
        DEFINE_INT_PROP(sys, epoc::DISK_LEVEL_CATEGORY, epoc::RAM_DISK_CRITICAL_THRESHOLD_KEY,
            epoc::RAM_DISK_CRITICAL_THRESHOLD);
        DEFINE_INT_PROP(sys, epoc::DISK_LEVEL_CATEGORY, epoc::OTHER_DISK_CRITICAL_THRESHOLD_KEY,
            epoc::OTHER_DISK_CRITICAL_THRESHOLD);

        DEFINE_BIN_PROP(sys, epoc::SYS_CATEGORY, epoc::LOCALE_LANG_KEY, sizeof(epoc::locale_language), lang);
        DEFINE_BIN_PROP(sys, epoc::SYS_CATEGORY, epoc::LOCALE_DATA_KEY, sizeof(epoc::locale), locale);
        DEFINE_BIN_PROP(sys, epoc::SYS_CATEGORY, epoc::LOCALE_LOCALE_SETTINGS_KEY, sizeof(epoc::locale_locale_settings), locale_settings);
    }
}

namespace eka2l1 {
    // EKA2L1_STUB_SERVERS="<name>[;<name>...]" (diagnostic): register an HLE server of each name that
    // accepts every connection, completes every synchronous request with KErrNone and leaves every
    // asynchronous one pending. It stands in for a ROM server that cannot run yet, so a wall behind
    // it can be reached (race ahead) while the real server is being fixed. Every request is logged.
    class stub_server : public service::typical_server {
    public:
        explicit stub_server(eka2l1::system *sys, const std::string &name)
            : service::typical_server(sys, name) {
        }

        void connect(service::ipc_context &context) override;
    };

    struct stub_session : public service::typical_session {
        explicit stub_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version)
            : service::typical_session(serv, ss_id, client_version) {
        }

        void fetch(service::ipc_context *ctx) override {
            const std::string client = ctx->msg->own_thr ? ctx->msg->own_thr->name() : std::string("?");
            if (ctx->msg->type == ipc_message_type_sync) {
                LOG_WARN(KERNEL, "Stub server {}: op {} from {} completed with KErrNone", server<stub_server>()->name(),
                    ctx->msg->function, client);
                ctx->complete(epoc::error_none);
                return;
            }

            LOG_WARN(KERNEL, "Stub server {}: async op {} from {} left pending", server<stub_server>()->name(),
                ctx->msg->function, client);
        }
    };

    void stub_server::connect(service::ipc_context &context) {
        create_session<stub_session>(&context);
        context.complete(epoc::error_none);
    }

    static void create_stub_servers(system *sys) {
        const char *env = std::getenv("EKA2L1_STUB_SERVERS");
        std::string list = env ? env : "";

        // Series 80: the ROM's PhoneServer.exe ("Phone Server", the call-handling server the Telephone
        // app and EikSrvUi connect to) cannot run without a cellular modem: it exits -5 right after it
        // opens the TSY's custom API, and the Telephone app then panics "PhoneServer start 1". A
        // phone-less stand-in is enough for everything that talks to it: Telephone sends op 300 once
        // (synchronous) while it builds its first screen, EikSrvUi ops 0 and 300. The stub answers
        // KErrNone and leaves notifications pending, as for a phone that never rings.
        // EKA2L1_NO_HLE_PHONESERVER=1 leaves the name to the ROM's server.
        kernel_system *kern = sys->get_kernel_system();
        if (kern->is_eka1() && sys->is_s80_device_active() && (std::getenv("EKA2L1_NO_HLE_PHONESERVER") == nullptr)
            && (list.find("Phone Server") == std::string::npos)) {
            list += list.empty() ? "Phone Server" : ";Phone Server";
        }

        if (list.empty()) {
            return;
        }

        std::size_t start = 0;
        while (start < list.size()) {
            std::size_t end = list.find(';', start);
            if (end == std::string::npos) {
                end = list.size();
            }

            const std::string name = list.substr(start, end - start);
            start = end + 1;
            if (name.empty()) {
                continue;
            }

            std::unique_ptr<service::server> svr = std::make_unique<stub_server>(sys, name);
            sys->get_kernel_system()->add_custom_server(svr);
            LOG_WARN(KERNEL, "Stub server {} registered", name);
        }
    }
}

namespace eka2l1 {
    namespace service {
        // Mostly replace startup process of a normal EPOC startup
        void init_services(system *sys) {
            if (sys->get_kernel_system()->is_eka1()) {
                kernel_system *kern = sys->get_kernel_system();
                auto posix_servers = std::make_shared<std::unordered_map<kernel::uid, posix_server *>>();

                kern->register_codeseg_loaded_callback([sys, posix_servers](const std::string &, kernel::process *process,
                                                           codeseg_ptr code_segment) {
                    // Patch libraries are attached globally while the device boots and do not
                    // belong to a guest process.
                    if (!process) {
                        return;
                    }

                    const auto existing_server = posix_servers->find(process->unique_id());
                    if (existing_server != posix_servers->end()) {
                        // EKA1 GUI applications run inside AppRun.exe. Once its .app image is
                        // attached, that image determines the POSIX current drive rather than
                        // AppRun's Z: drive.
                        if (common::lowercase_ucs2_string(eka2l1::path_extension(code_segment->get_full_path()))
                            == u".app") {
                            existing_server->second->update_executable_path(code_segment->get_full_path());
                        }
                        return;
                    }

                    if (code_segment != process->get_codeseg()) {
                        return;
                    }

                    std::unique_ptr<posix_server> server = std::make_unique<posix_server>(sys, process, code_segment->get_full_path());
                    (*posix_servers)[process->unique_id()] = server.get();
                    std::unique_ptr<service::server> generic_server = std::move(server);
                    sys->get_kernel_system()->add_custom_server(generic_server);
                });

                kern->register_process_exit_callback([posix_servers](kernel::process *process) {
                    const auto server = posix_servers->find(process->unique_id());
                    if (server == posix_servers->end()) {
                        return;
                    }

                    // Process exit callbacks run before its session handles are released.
                    // Removing the HLE server here would leave those sessions pointing at
                    // freed memory. The server has a process-unique name, so only remove it
                    // from the lookup map and let normal kernel teardown own its lifetime.
                    posix_servers->erase(server);
                });
            }

            CREATE_SERVER_D(sys, fs_server);
            CREATE_SERVER(sys, loader_server);
            CREATE_SERVER(sys, shutdown_server);

            if (sys->get_symbian_version_use() == epocver::epoc70) {
                // Platform services UIQ applications block on; the ROM starts them at boot.
                CREATE_SERVER(sys, memory_manager_server);
                CREATE_SERVER(sys, linnea_server);
            }

            if (sys->get_kernel_system()->is_eka1()) {
                CREATE_SERVER(sys, camera_server);
            }

            config::state *cfg = sys->get_config();

            CREATE_SERVER(sys, fbs_server);
            CREATE_SERVER(sys, window_server);
            CREATE_SERVER(sys, central_repo_server, provide_host_access_point);
            CREATE_SERVER(sys, featmgr_server);

            if (cfg->enable_srv_rights)
                CREATE_SERVER(sys, rights_server);

            if (cfg->enable_srv_sa)
                CREATE_SERVER(sys, sa_server);

            if (cfg->enable_srv_drm)
                CREATE_SERVER(sys, drm_helper_server);

            // EKA2L1_ROM_EIKSRV=1 (experiment): leave the Eikon server to the ROM (eiksrvs.exe on
            // Series 80 v2), so its EikSrvUi owns the application buttons and the task list. The ROM
            // server also brings up the notifier and view servers itself, and a pre-registered HLE of
            // either name makes that construction leave with KErrAlreadyExists.
            const bool rom_eiksrv = std::getenv("EKA2L1_ROM_EIKSRV") != nullptr
                || (sys->get_symbian_version_use() == epocver::epoc7 && std::getenv("EKA2L1_ROM_WSERV"));

            // These needed to be HLEd
            CREATE_SERVER(sys, applist_server);
            CREATE_SERVER(sys, oom_ui_app_server);
            CREATE_SERVER(sys, hwrm_server);
            if (!rom_eiksrv) {
                CREATE_SERVER(sys, view_server);
            }
            CREATE_SERVER(sys, remcon_server);
            CREATE_SERVER(sys, etel_server);
            if (!rom_eiksrv) {
                CREATE_SERVER(sys, notifier_server);
            }
            CREATE_SERVER(sys, msv_server);

            CREATE_SERVER(sys, sensor_server);
            CREATE_SERVER(sys, connmonitor_server);
            CREATE_SERVER(sys, nifman_server);
            CREATE_SERVER(sys, drm_notifier_server);
            CREATE_SERVER(sys, sisregistry_server);
            CREATE_SERVER(sys, alarm_server);

            // The ROM's Eikon server hosts AlarmAlertServer itself; an HLE of that name created first
            // would take its clients (the ROM AlarmServer) away from it.
            if ((sys->get_symbian_version_use() == epocver::epoc7) && !rom_eiksrv) {
                CREATE_SERVER(sys, alarm_alert_server_eka1);
            }
            CREATE_SERVER(sys, socket_server);

            CREATE_SERVER(sys, comm_server);
            CREATE_SERVER(sys, bt_server);
            CREATE_SERVER(sys, btman_server);
            CREATE_SERVER(sys, accessory_server);

            // Not really sure about this one
            CREATE_SERVER(sys, keysound_server);

            if (!rom_eiksrv) {
                CREATE_SERVER(sys, eikappui_server);
            }
            // The AknIconServer HLE renders icons itself (lunasvg / mbm) instead of the guest
            // ROM server. It exists to work around N95-class S60v3 FP1 ROMs, whose guest icon
            // server rasterises scalable NVG menu icons through software OpenVG -- the emulator
            // has no GPU NVG, and that draw-device path only accepts 32bpp, so it leaves on the
            // standard 64K icon and aborts the Options menu. The HLE is not a complete drop-in
            // replacement, though: newer ROMs (e.g. Nokia 5320, FP2) render every icon fine via
            // the guest server but use icon-server requests the HLE doesn't fully implement, so
            // forcing them through it regresses their UI (blank Calculator). Only replace the
            // guest server where it is actually broken; let every other ROM keep its own.
            if (sys->get_symbian_version_use() == epocver::epoc93fp1) {
                CREATE_SERVER(sys, akn_icon_server);
            }
            CREATE_SERVER(sys, akn_skin_server);

            CREATE_SERVER(sys, system_agent_server);

            // Nokia's Domestic OS server: the ROM's own DosServer.exe would load Nokia.dsy
            // and wait for a cellular modem that is not here. Series 80 v2's boot chain
            // (Starter via SysUtil) asks it for the start-up reason before it starts a
            // single application, so answer like Nokia's phone-less SDK plug-in does.
            // EKA2L1_NO_HLE_DOS=1 leaves the ROM's server in charge.
            if (sys->get_kernel_system()->is_eka1() && sys->is_s80_device_active()
                && (std::getenv("EKA2L1_NO_HLE_DOS") == nullptr)) {
                CREATE_SERVER(sys, dos_server);
            }
            CREATE_SERVER(sys, unipertar_server);

            if (sys->get_symbian_version_use() >= epocver::epoc95) {
                CREATE_SERVER(sys, timezone_server);
            }

            if (sys->get_symbian_version_use() <= epocver::eka2) {
                CREATE_SERVER(sys, redir_server);
                CREATE_SERVER(sys, backup_old_server);
            } else {
                CREATE_SERVER(sys, goom_monitor_server);
                CREATE_SERVER(sys, alf_streamer_server);
                CREATE_SERVER(sys, dm_domain_server);

                // MMF server family
                {
                    std::unique_ptr<service::server> dev_serv = std::make_unique<mmf_dev_server>(sys);
                    std::unique_ptr<service::server> aud_serv = std::make_unique<mmf_audio_server>(sys,
                        reinterpret_cast<mmf_dev_server *>(dev_serv.get()));

                    kernel_system *kern = sys->get_kernel_system();
                    kern->add_custom_server(dev_serv);
                    kern->add_custom_server(aud_serv);
                }
            }

            create_stub_servers(sys);

            epoc::initialize_system_properties(sys, cfg);
            init_symbian_app_launch_to_host_launch(sys);
        }
        
        void init_services_post_bootup(system *sys) {
            epoc::sms::supply_sim_settings(sys);
            epoc::load_s80_persisted_locale(sys);
        }

        // EKA2L1_PRESTART="<path>[;<path>...]" starts ROM executables at boot, before any --run app, the
        // way the ROM's Starter would. With EKA2L1_ROM_EIKSRV on a Series 80 device the default is the one
        // server Starter brings up before HandleStartEikon that the Eikon server cannot construct
        // without: SecurityServer.exe (EikSrvUi connects to it while constructing; without it the
        // construction fails and EikSrv dies with USER 44). EKA2L1_PRESTART= (empty) starts nothing.
        void start_prestart_processes(system *sys) {
            kernel_system *kern = sys->get_kernel_system();
            if (!kern) {
                return;
            }

            std::string list;
            bool optional_entries = false;
            if (const char *env = std::getenv("EKA2L1_PRESTART")) {
                list = env;
            } else if ((std::getenv("EKA2L1_ROM_EIKSRV") || std::getenv("EKA2L1_ROM_WSERV")) && kern->is_eka1() && sys->is_s80_device_active()) {
                // SysState.exe (tools/s80-sysstate, ours) publishes the SharedData system state Starter
                // would have left (state.val=203 ...); without it the Eikon server's alarm alert server
                // refuses the ROM AlarmServer, which is then restarted twice a second. Optional: it is
                // started only when the data dir carries it.
                list = std::getenv("EKA2L1_ROM_WSERV") && !std::getenv("EKA2L1_ROM_STARTER")
                    ? "Z:\\System\\Programs\\SecurityServer.exe"
                    : "C:\\System\\Programs\\SysState.exe;Z:\\System\\Programs\\SecurityServer.exe";
                optional_entries = true;
            } else if (kern->is_eka1() && sys->is_s80_device_active()) {
                // With the HLE Eikon server nothing else starts SecurityServer (the PIN/security-code
                // server Starter launches on the device), and the Telephone app needs it: its
                // connection fails, it shows an error note and closes, leaving a black screen. The
                // server itself only needs ETel, which the HLE provides.
                list = "Z:\\System\\Programs\\SecurityServer.exe";
            }

            // EKA2L1_ROM_WSERV is a default-off EKA1/S80 feasibility experiment. The
            // host window object is only a display adapter in this mode; ewsrv owns
            // Windowserver. Keep HLE FBS for the first experiment.
            if (sys->get_symbian_version_use() == epocver::epoc7 && std::getenv("EKA2L1_ROM_WSERV")) {
                list = "Z:\\System\\Libs\\ewsrv.exe;" + list;
            }

            std::size_t start = 0;
            while (start < list.size()) {
                std::size_t end = list.find(';', start);
                if (end == std::string::npos) {
                    end = list.size();
                }

                const std::string path = list.substr(start, end - start);
                start = end + 1;

                if (path.empty()) {
                    continue;
                }

                if (optional_entries && (path[0] == 'C') && !sys->get_io_system()->exist(common::utf8_to_ucs2(path))) {
                    LOG_INFO(KERNEL, "Prestart: {} not present, skipped", path);
                    continue;
                }

                kernel::process *pr = kern->spawn_new_process(common::utf8_to_ucs2(path));
                if (!pr) {
                    LOG_ERROR(KERNEL, "Prestart: unable to launch {}", path);
                    continue;
                }

                pr->run();
                LOG_INFO(KERNEL, "Prestart: launched {}", path);
            }
        }
    }
}
