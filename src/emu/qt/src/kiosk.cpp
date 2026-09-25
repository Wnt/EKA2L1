#include <services/window/rom_bridge.h>
#include <common/pystr.h>
/*
 * Copyright (c) 2026 EKA2L1 Team.
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

#include <qt/kiosk.h>
#include <qt/mainwindow.h>
#include <qt/state.h>
#include <qt/utils.h>

#include <common/arghandler.h>
#include <common/cvt.h>
#include <common/log.h>

#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <services/applist/applist.h>
#include <services/ui/cap/oom_app.h>
#include <services/window/classes/wingroup.h>
#include <services/window/screen.h>
#include <services/window/window.h>
#include <system/devices.h>
#include <system/epoc.h>
#include <utils/apacmd.h>

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QString>
#include <sstream>
#include <unordered_map>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <regex>

#include <sys/stat.h>

namespace eka2l1::desktop {
    static std::string launch_directory;

    void set_launch_directory(const std::string &dir) {
        launch_directory = dir;
    }

    std::string resolve_launch_path(const std::string &path) {
        if (path.empty() || (path[0] == '/') || launch_directory.empty()) {
            return path;
        }

        return launch_directory + "/" + path;
    }

    void prescan_early_options(emulator &state, const int argc, const char **argv) {
        for (int i = 1; i < argc; i++) {
            const std::string token = argv[i];

            if ((token == "--log-file") && (i + 1 < argc)) {
                state.log_file_path = resolve_launch_path(argv[++i]);
            } else if ((token == "--log-filter") && (i + 1 < argc)) {
                state.log_filter_override = argv[++i];
            } else if (token == "--no-console-log") {
                state.console_log = false;
            }
        }
    }

    // ------------------------------------------------------------------ option parsing

    static emulator *emu_from(void *userdata) {
        return reinterpret_cast<emulator *>(userdata);
    }

    static bool parse_int(const std::string &str, int &out) {
        if (str.empty()) {
            return false;
        }

        char *end = nullptr;
        const long value = std::strtol(str.c_str(), &end, 10);

        if (!end || *end) {
            return false;
        }

        out = static_cast<int>(value);
        return true;
    }

    // WxH, WxH+X+Y or +X+Y. A zero W or H stands for the magnified screen's.
    static bool parse_geometry(const std::string &str, kiosk_options &kiosk) {
        static const std::regex full_re("^([0-9]+)x([0-9]+)(\\+(-?[0-9]+)\\+(-?[0-9]+))?$");
        static const std::regex pos_re("^\\+(-?[0-9]+)\\+(-?[0-9]+)$");

        std::smatch match;

        if (std::regex_match(str, match, full_re)) {
            kiosk.window_width = std::atoi(match[1].str().c_str());
            kiosk.window_height = std::atoi(match[2].str().c_str());

            if (match[3].matched) {
                kiosk.window_x = std::atoi(match[4].str().c_str());
                kiosk.window_y = std::atoi(match[5].str().c_str());
            }

            kiosk.geometry_given = true;
            return true;
        }

        if (std::regex_match(str, match, pos_re)) {
            kiosk.window_x = std::atoi(match[1].str().c_str());
            kiosk.window_y = std::atoi(match[2].str().c_str());
            kiosk.geometry_given = true;

            return true;
        }

        return false;
    }

    static bool parse_colour(std::string str, std::uint32_t &argb) {
        if (!str.empty() && (str[0] == '#')) {
            str = str.substr(1);
        } else if ((str.size() > 2) && (str[0] == '0') && ((str[1] == 'x') || (str[1] == 'X'))) {
            str = str.substr(2);
        }

        if ((str.size() != 6) || !std::all_of(str.begin(), str.end(), [](const char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
            return false;
        }

        argb = 0xFF000000u | static_cast<std::uint32_t>(std::strtoul(str.c_str(), nullptr, 16));
        return true;
    }

    // Every option below takes its value as the NEXT argument (the parser splits on spaces only).
    void register_museum_options(common::arg_parser &parser) {
        parser.add("--kiosk", "Museum kiosk: the window shows only the emulated screen - no menu bar, status bar,\n"
                              "\t\t\t  frame, tray icon or dialog - magnified by an integer factor with nearest-neighbour\n"
                              "\t\t\t  filtering on a plain background, at a fixed size and position. Host shortcuts are off.",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                emu_from(userdata)->kiosk.enabled = true;
                return true;
            });

        parser.add("--kiosk-scale", "Kiosk magnification, an integer 1-16 (default 2). Implies --kiosk.\n"
                                    "\t\t\t  Example: --kiosk-scale 2   (a 640x200 screen fills 1280x400)",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                const char *value = parser->next_token();
                int scale = 0;

                if (!value || !parse_int(value, scale) || (scale < 1) || (scale > 16)) {
                    *err = "--kiosk-scale needs an integer between 1 and 16";
                    return false;
                }

                emulator *emu = emu_from(userdata);
                emu->kiosk.enabled = true;
                emu->kiosk.scale = scale;

                return true;
            });

        parser.add("--kiosk-geometry", "Kiosk window size and position as WxH+X+Y, WxH or +X+Y. A 0 width or height is the\n"
                                       "\t\t\t  magnified screen's. Default: the magnified screen's size at +0+0. Implies --kiosk.\n"
                                       "\t\t\t  Example: --kiosk-geometry 1280x400+0+0",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                const char *value = parser->next_token();
                emulator *emu = emu_from(userdata);

                if (!value || !parse_geometry(value, emu->kiosk)) {
                    *err = "--kiosk-geometry needs WxH+X+Y, WxH or +X+Y";
                    return false;
                }

                emu->kiosk.enabled = true;
                return true;
            });

        parser.add("--kiosk-offset", "Where the magnified screen sits inside the kiosk window, as X,Y (default: centred).\n"
                                     "\t\t\t  Implies --kiosk. Example: --kiosk-offset 0,0",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                static const std::regex offset_re("^(-?[0-9]+),(-?[0-9]+)$");

                const char *value = parser->next_token();
                const std::string str = value ? value : "";
                std::smatch match;

                if (!std::regex_match(str, match, offset_re)) {
                    *err = "--kiosk-offset needs X,Y";
                    return false;
                }

                emulator *emu = emu_from(userdata);
                emu->kiosk.enabled = true;
                emu->kiosk.offset_x = std::atoi(match[1].str().c_str());
                emu->kiosk.offset_y = std::atoi(match[2].str().c_str());

                return true;
            });

        parser.add("--kiosk-background", "Kiosk fill colour around the screen as RRGGBB (optionally #RRGGBB), default 000000.\n"
                                         "\t\t\t  Implies --kiosk. Example: --kiosk-background 000000",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                const char *value = parser->next_token();
                emulator *emu = emu_from(userdata);

                if (!value || !parse_colour(value, emu->kiosk.background)) {
                    *err = "--kiosk-background needs a colour as RRGGBB";
                    return false;
                }

                emu->kiosk.enabled = true;
                return true;
            });

        parser.add("--kiosk-home", "Kiosk home app (UID as 0x... or caption): started if --run gives none, relaunched\n"
                                   "\t\t\t  whenever the last app exits, and by the control channel's reset. Without it the\n"
                                   "\t\t\t  emulator quits (exit code 0) when the --run app exits. Implies --kiosk.\n"
                                   "\t\t\t  Example: --kiosk-home 0x101f8e4f",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                const char *value = parser->next_token();

                if (!value) {
                    *err = "--kiosk-home needs an app UID or caption";
                    return false;
                }

                emulator *emu = emu_from(userdata);
                emu->kiosk.enabled = true;
                emu->kiosk.home_app = value;

                return true;
            });

        parser.add("--control-socket", "Serve the ekactl/1 control protocol on this Unix-domain socket path (relative paths\n"
                                       "\t\t\t  resolve against the launch directory). Verbs: help ping apps list focus launch\n"
                                       "\t\t\t  switch screenshot stats reset quit. Example: --control-socket /run/eka/ctl.sock",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                const char *value = parser->next_token();

                if (!value) {
                    *err = "--control-socket needs a path";
                    return false;
                }

                emu_from(userdata)->control_socket_path = resolve_launch_path(value);
                return true;
            });

        parser.add("--log-file", "Write the log to this file instead of EKA2L1.log in the data directory (truncated at\n"
                                 "\t\t\t  start; relative paths resolve against the launch directory).",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                // Applied before stage one by prescan_early_options(); only consumed here.
                if (!parser->next_token()) {
                    *err = "--log-file needs a path";
                    return false;
                }

                return true;
            });

        parser.add("--log-filter", "Replace config.yml's log-filter for this run. Example: --log-filter \"*:warn\"",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                if (!parser->next_token()) {
                    *err = "--log-filter needs a filter string";
                    return false;
                }

                return true;
            });

        parser.add("--no-console-log", "Do not copy the log to standard output.",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                return true;
            });

        parser.add("--no-update-check", "Never contact the update server (implied by --kiosk and --run).",
            [](common::arg_parser *parser, void *userdata, std::string *err) {
                emu_from(userdata)->no_update_check = true;
                return true;
            });
    }

    // ------------------------------------------------------------------ apps and window groups

    applist_server *get_applist_server(emulator &state) {
        kernel_system *kern = state.symsys ? state.symsys->get_kernel_system() : nullptr;
        if (!kern) {
            return nullptr;
        }

        return reinterpret_cast<applist_server *>(kern->get_by_name<service::server>(
            get_app_list_server_name_by_epocver(kern->get_epoc_version())));
    }

    static std::string lowercase(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return str;
    }

    static std::string trim(const std::string &str) {
        const std::size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "";
        }

        const std::size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }

    static bool parse_uid(const std::string &spec, std::uint32_t &uid) {
        if (spec.empty()) {
            return false;
        }

        char *end = nullptr;
        unsigned long value = 0;

        if ((spec.size() > 2) && (spec[0] == '0') && ((spec[1] == 'x') || (spec[1] == 'X'))) {
            value = std::strtoul(spec.c_str() + 2, &end, 16);
        } else if (std::all_of(spec.begin(), spec.end(), [](const char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            value = std::strtoul(spec.c_str(), &end, 10);
        } else {
            return false;
        }

        if (!end || *end) {
            return false;
        }

        uid = static_cast<std::uint32_t>(value);
        return true;
    }

    static std::string caption_of(apa_app_registry &reg, const bool long_caption) {
        epoc::apa_app_caption &caption = long_caption ? reg.mandatory_info.long_caption : reg.mandatory_info.short_caption;
        return common::ucs2_to_utf8(caption.to_std_string(nullptr));
    }

    static std::string hex_uid(const std::uint32_t uid) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08x", uid);
        return buf;
    }

    apa_app_registry *find_app_registry(applist_server *svr, const std::string &raw_spec, std::string *err) {
        if (!svr) {
            *err = "no app list server (is a device installed and booted?)";
            return nullptr;
        }

        const std::string spec = trim(raw_spec);
        std::uint32_t uid = 0;

        if (parse_uid(spec, uid)) {
            apa_app_registry *reg = svr->get_registration(uid);
            if (!reg) {
                *err = "no installed app has UID " + hex_uid(uid);
            }

            return reg;
        }

        std::vector<apa_app_registry> &regs = svr->get_registerations();
        const std::string wanted = lowercase(spec);

        // Exact long caption, exact short caption, then either one without case.
        for (int pass = 0; pass < 3; pass++) {
            for (apa_app_registry &reg : regs) {
                const std::string long_caption = caption_of(reg, true);
                const std::string short_caption = caption_of(reg, false);

                const bool hit = (pass == 0) ? (long_caption == spec)
                    : (pass == 1)            ? (short_caption == spec)
                                             : ((lowercase(long_caption) == wanted) || (lowercase(short_caption) == wanted));

                if (hit) {
                    return &reg;
                }
            }
        }

        *err = "no installed app is called '" + spec + "' (see the apps verb or --listapp)";
        return nullptr;
    }

    bool launch_app(emulator &state, const std::string &spec, const bool track_exit, running_app *launched,
        std::string *err, const std::string &document) {
        kernel_system *kern = state.symsys ? state.symsys->get_kernel_system() : nullptr;
        if (!kern) {
            *err = "no kernel (is a device installed?)";
            return false;
        }

        const std::lock_guard<kernel_system> guard(*kern);

        applist_server *svr = get_applist_server(state);
        apa_app_registry *reg = find_app_registry(svr, spec, err);

        if (!reg) {
            return false;
        }

        epoc::apa::command_line cmdline;
        cmdline.launch_cmd_ = epoc::apa::command_create;

        if (!document.empty()) {
            // How one S80 app hands another a file or a URL: the document of an Open command.
            cmdline.launch_cmd_ = epoc::apa::command_open;
            cmdline.document_name_ = common::utf8_to_ucs2(document);
        }

        std::function<void(kernel::process *)> exit_callback = nullptr;
        if (track_exit) {
            emulator *emu = &state;
            exit_callback = [emu](kernel::process *pr) {
                // Runs on the OS thread under the kernel lock: only queue a signal from here.
                if (emu->ui_main) {
                    emu->ui_main->get_process_exit_callback()(pr);
                }
            };
        }

        const std::string name = caption_of(*reg, true);

        if (!svr->launch_app(*reg, cmdline, nullptr, exit_callback)) {
            *err = "the app list server could not start " + name;
            return false;
        }

        LOG_INFO(FRONTEND_UI, "Museum frontend: launched {} ({}){}{}", name, hex_uid(reg->mandatory_info.uid),
            document.empty() ? "" : " with document ", document);

        if (launched) {
            launched->uid = reg->mandatory_info.uid;
            launched->name = name;
        }

        return true;
    }

    void split_app_and_document(emulator &state, const std::string &arg, std::string &spec, std::string &document) {
        spec = trim(arg);
        document.clear();

        const std::size_t space = spec.find_first_of(" \t");
        if (space == std::string::npos) {
            return;
        }

        kernel_system *kern = state.symsys ? state.symsys->get_kernel_system() : nullptr;
        if (kern) {
            const std::lock_guard<kernel_system> guard(*kern);
            std::string ignored;

            if (find_app_registry(get_applist_server(state), spec, &ignored)) {
                return;
            }
        }

        document = trim(spec.substr(space + 1));
        spec = spec.substr(0, space);
    }

    // Caller holds the kernel lock.
    static void collect_groups(epoc::screen *scr, const bool include_plain_groups, std::vector<running_app> &apps) {
        int ordinal = 0;

        for (epoc::window_group *group = scr->get_group_chain(); group;
             group = reinterpret_cast<epoc::window_group *>(group->sibling), ordinal++) {
            if (group->type != epoc::window_kind::group) {
                continue;
            }

            kernel::thread *owner = (group->client) ? group->client->get_client() : nullptr;
            kernel::process *pr = owner ? owner->owning_process() : nullptr;

            // A group whose process is on its way out is not something to switch to.
            if (!pr || (pr->get_exit_type() != kernel::entity_exit_type::pending)) {
                continue;
            }

            running_app app;
            std::optional<akn_running_app_info> info = get_akn_app_info_from_window_group(group);

            if (info.has_value()) {
                app.uid = info->app_uid_;
                app.name = common::ucs2_to_utf8(info->app_name_);
            } else {
                if (!include_plain_groups) {
                    continue;
                }

                std::u16string name = group->name;
                std::replace(name.begin(), name.end(), u'\0', u'|');

                app.uid = pr->get_uid();
                app.name = common::ucs2_to_utf8(name);
                app.is_app = false;
            }

            app.window_group = group->id;
            app.ordinal = ordinal;
            app.focus = (group == scr->focus);
            app.process = pr->unique_id();

            apps.push_back(app);
        }
    }

    static void collect_rom_groups(kernel_system *kern, rom_window_bridge *bridge,
        const bool include_plain_groups, std::vector<running_app> &apps) {
        int ordinal = 0;
        for (const auto &row : bridge->snapshot.groups) {
            const int position = ordinal++;
            auto *owner = kern->get_by_id<kernel::thread>(row.thread);
            auto *pr = owner ? owner->owning_process() : nullptr;
            if (!pr || pr->get_exit_type() != kernel::entity_exit_type::pending) continue;
            std::u16string name(row.name, row.name + row.name_length);
            const auto parts = common::pystr16(name).split(u'\0');
            running_app app;
            if (parts.size() >= 3) {
                app.uid = parts[1].as_int<std::uint32_t>(0, 16);
                app.name = common::ucs2_to_utf8(parts[2].std_str());
            }
            if (!app.uid) {
                if (!include_plain_groups) continue;
                app.uid = pr->get_uid();
                std::replace(name.begin(), name.end(), u'\0', u'|');
                app.name = common::ucs2_to_utf8(name);
                app.is_app = false;
            }
            app.window_group = row.id;
            app.ordinal = position;
            app.focus = row.id == bridge->snapshot.focus;
            app.process = pr->unique_id();
            apps.push_back(std::move(app));
        }
    }

    std::vector<running_app> list_running_apps(emulator &state, const bool include_plain_groups) {
        std::vector<running_app> apps;
        kernel_system *kern = state.symsys ? state.symsys->get_kernel_system() : nullptr;

        if (!kern) {
            return apps;
        }

        const std::lock_guard<kernel_system> guard(*kern);
        epoc::screen *scr = get_current_active_screen(state.symsys.get(), 0);

        if (auto *bridge = get_rom_window_bridge(kern)) {
            collect_rom_groups(kern, bridge, include_plain_groups, apps);
        } else if (scr) {
            collect_groups(scr, include_plain_groups, apps);
        }

        return apps;
    }

    switch_result switch_to_app(emulator &state, const std::string &spec, running_app *result, std::string *err) {
        kernel_system *kern = state.symsys ? state.symsys->get_kernel_system() : nullptr;
        if (!kern) {
            *err = "no kernel (is a device installed?)";
            return switch_result::failed;
        }

        {
            const std::lock_guard<kernel_system> guard(*kern);

            applist_server *svr = get_applist_server(state);
            std::string lookup_err;
            apa_app_registry *reg = find_app_registry(svr, spec, &lookup_err);

            epoc::screen *scr = get_current_active_screen(state.symsys.get(), 0);
            if (!scr) {
                *err = "no screen";
                return switch_result::failed;
            }

            std::vector<running_app> apps;
            auto *bridge = get_rom_window_bridge(kern);
            if (bridge) {
                if (!bridge->snapshot.ready) {
                    *err = "ROM window bridge not ready (install the current SysState.exe)";
                    return switch_result::failed;
                }
                collect_rom_groups(kern, bridge, false, apps);
            } else {
                collect_groups(scr, false, apps);
            }

            const std::string wanted = lowercase(trim(spec));
            const running_app *target = nullptr;

            for (const running_app &app : apps) {
                // By the registry's UID when the spec names an installed app, else by the group's caption.
                if (reg ? (app.uid == reg->mandatory_info.uid) : (lowercase(app.name) == wanted)) {
                    target = &app;
                    break;
                }
            }

            if (target) {
                if (bridge) {
                    if (bridge->switches.size() >= 64) {
                        *err = "ROM window switch queue full";
                        return switch_result::failed;
                    }
                    bridge->switches.push_back(target->window_group);
                    if (result) *result = *target;
                    return switch_result::queued;
                }
                window_server *ws = get_window_server_through_system(state.symsys.get());
                epoc::window_group *group = ws ? ws->get_group_from_id(target->window_group) : nullptr;

                if (!group || (group->type != epoc::window_kind::group)) {
                    *err = "window group vanished";
                    return switch_result::failed;
                }

                // What the Symbian task switcher does: RWsSession::SetWindowGroupOrdinalPosition(id, 0).
                group->set_position(0);

                if (result) {
                    *result = *target;
                    result->ordinal = group->ordinal_position(false);
                    result->focus = (scr->focus == group);
                }

                LOG_INFO(FRONTEND_UI, "Museum frontend: switched to {} ({}), window group {}", target->name,
                    hex_uid(target->uid), target->window_group);

                return switch_result::switched;
            }

            if (!reg) {
                *err = lookup_err;
                return switch_result::failed;
            }
        }

        // Not running: start it (launch_app takes the kernel lock itself).
        if (!launch_app(state, spec, state.kiosk.enabled, result, err)) {
            return switch_result::failed;
        }

        return switch_result::launched;
    }

    bool refresh_screen(emulator &state, std::string *err) {
        kernel_system *kern = state.symsys ? state.symsys->get_kernel_system() : nullptr;
        if (!kern) {
            *err = "no kernel";
            return false;
        }

        const std::lock_guard<kernel_system> guard(*kern);

        window_server *ws = get_window_server_through_system(state.symsys.get());
        epoc::screen *scr = get_current_active_screen(state.symsys.get(), 0);

        if (!ws || !scr) {
            *err = "no window server screen";
            return false;
        }

        // What an ordinal change does (window::move_window), without changing the order.
        scr->need_update_visible_regions(true);
        scr->flags_ |= epoc::screen::FLAG_SERVER_REDRAW_PENDING;
        ws->get_anim_scheduler()->schedule(ws->get_graphics_driver(), scr, ws->get_ntimer()->microseconds());

        return true;
    }

    std::string screen_diagnostics(emulator &state) {
        kernel_system *kern = state.symsys ? state.symsys->get_kernel_system() : nullptr;
        if (!kern) {
            return "screen=none";
        }

        const std::lock_guard<kernel_system> guard(*kern);
        epoc::screen *scr = get_current_active_screen(state.symsys.get(), 0);

        if (!scr) {
            return "screen=none";
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf), "tex=%llu dsf=%.2f lsf=%.2f flags=0x%x refresh=%u",
            static_cast<unsigned long long>(scr->screen_texture), scr->display_scale_factor, scr->logic_scale_factor_x,
            scr->flags_, static_cast<unsigned>(scr->refresh_rate));

        return buf;
    }

    // ------------------------------------------------------------------ control channel

    static std::string json_escape(const std::string &str) {
        std::string out;
        out.reserve(str.size() + 2);

        for (const char c : str) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
            }
        }

        return out;
    }

    static std::string app_json(const running_app &app) {
        return "{\"uid\":\"" + hex_uid(app.uid) + "\",\"name\":\"" + json_escape(app.name) + "\",\"wg\":"
            + std::to_string(app.window_group) + ",\"ordinal\":" + std::to_string(app.ordinal) + ",\"focus\":"
            + (app.focus ? "true" : "false") + ",\"app\":" + (app.is_app ? "true" : "false") + "}";
    }

    control_server::control_server(emulator &state)
        : state_(state)
        , server_(new QLocalServer())
        , input_timer_(new QTimer(server_)) {
        input_timer_->setSingleShot(true);
        QObject::connect(input_timer_, &QTimer::timeout, server_, [this]() {
            if (input_commands_.empty()) return;
            auto command = std::move(input_commands_.front());
            input_commands_.pop_front();
            command();
            input_timer_->start(30);
        });
        QObject::connect(server_, &QLocalServer::newConnection, server_, [this]() {
            on_new_connection();
        });
    }

    void control_server::enqueue_input(std::function<void()> command) {
        // The ARM wserv has a small per-client queue. Give the app a turn between
        // synthetic characters, as a physical keyboard does. Do not purge raw edges.
        if (input_commands_.empty() && !input_timer_->isActive()) {
            command();
            input_timer_->start(30);
        } else {
            input_commands_.push_back(std::move(command));
        }
    }

    control_server::~control_server() {
        server_->close();
        delete server_;
    }

    bool control_server::listen(const std::string &path, std::string *err) {
        struct stat st;

        // Only ever replace a stale socket, never a file someone mistyped the path to.
        if (lstat(path.c_str(), &st) == 0) {
            if (!S_ISSOCK(st.st_mode)) {
                *err = path + " exists and is not a socket";
                return false;
            }

            std::remove(path.c_str());
        }

        server_->setSocketOptions(QLocalServer::WorldAccessOption);

        if (!server_->listen(QString::fromStdString(path))) {
            *err = server_->errorString().toStdString();
            return false;
        }

        path_ = path;
        LOG_INFO(FRONTEND_UI, "Museum frontend: ekactl/1 listening on {}", path);

        return true;
    }

    std::string control_server::hello_line() {
        std::string device = "none";

        if (state_.symsys) {
            if (eka2l1::device *dvc = state_.symsys->get_device_manager()->get_current()) {
                device = dvc->firmware_code;
            }
        }

        const int view_w = state_.view_width.load();
        const int view_h = state_.view_height.load();

        std::string line = "HELLO ekactl/1 device=" + device + " screen=" + std::to_string(state_.screen_width.load()) + "x"
            + std::to_string(state_.screen_height.load()) + " view=" + std::to_string(view_w) + "x" + std::to_string(view_h)
            + "+" + std::to_string(state_.view_x.load()) + "+" + std::to_string(state_.view_y.load());

        if (state_.kiosk.enabled) {
            line += " scale=" + std::to_string(state_.kiosk.scale);
        }

        if (state_.ui_main) {
            const QRect geo = state_.ui_main->geometry();
            line += " window=" + std::to_string(geo.width()) + "x" + std::to_string(geo.height()) + "+"
                + std::to_string(geo.x()) + "+" + std::to_string(geo.y());
        }

        return line;
    }

    void control_server::on_new_connection() {
        while (QLocalSocket *socket = server_->nextPendingConnection()) {
            pending_[socket] = std::string();

            QObject::connect(socket, &QLocalSocket::readyRead, server_, [this, socket]() {
                on_ready_read(socket);
            });

            QObject::connect(socket, &QLocalSocket::disconnected, server_, [this, socket]() {
                pending_.erase(socket);
                socket->deleteLater();
            });

            const std::string hello = hello_line() + "\n";
            socket->write(hello.data(), static_cast<qint64>(hello.size()));
            socket->flush();
        }
    }

    void control_server::on_ready_read(QLocalSocket *socket) {
        auto it = pending_.find(socket);
        if (it == pending_.end()) {
            return;
        }

        const QByteArray data = socket->readAll();
        it->second.append(data.constData(), static_cast<std::size_t>(data.size()));

        std::size_t newline = 0;

        while ((newline = it->second.find('\n')) != std::string::npos) {
            std::string line = it->second.substr(0, newline);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos) line.clear();
            else line.erase(0, first);
            it->second.erase(0, newline + 1);

            if (line.empty()) {
                continue;
            }

            bool quit_after = false;
            const std::string reply = execute(line, quit_after) + "\n";

            socket->write(reply.data(), static_cast<qint64>(reply.size()));
            socket->flush();

            if (quit_after) {
                LOG_INFO(FRONTEND_UI, "Museum frontend: quit requested on the control socket");
                QTimer::singleShot(50, []() {
                    QCoreApplication::quit();
                });

                return;
            }
        }

        if (it->second.size() > 65536) {
            it->second.clear();

            const std::string reply = "ERR line too long\n";
            socket->write(reply.data(), static_cast<qint64>(reply.size()));
        }
    }

    std::string control_server::execute(const std::string &line, bool &quit_after) {
        const std::size_t space = line.find_first_of(" \t");
        const std::string verb = lowercase(line.substr(0, space));
        const std::string arg = (space == std::string::npos) ? std::string()
            : (verb == "type" ? line.substr(space + 1) : trim(line.substr(space + 1)));

        std::string err;

        if (verb == "ping") {
            return "OK pong";
        }

        if (verb == "help") {
            return "OK verbs: ping | apps | list [all] | focus | launch <uid|caption> | switch <uid|caption> | "
                   "key <name> [down|up] | type <UTF-8 text> | screenshot <path> [native] | stats | refresh | reset | quit";
        }

        if (verb == "key" || verb == "type") {
            if (!state_.winserv) return "ERR no window server";
            if (input_commands_.size() + arg.size() > 65536) return "ERR input queue full";
            auto edge = [this](std::uint32_t code, std::uint32_t text, bool down) {
                if (!state_.winserv) return;
                drivers::input_event event{};
                event.type_ = drivers::input_event_type::key;
                event.key_.code_ = code;
                event.key_.text_ = text;
                event.key_.state_ = down ? drivers::key_state::pressed : drivers::key_state::released;
                state_.winserv->queue_input_from_driver(event);
            };
            if (verb == "type") {
                const auto chars = QString::fromUtf8(arg.data(), static_cast<int>(arg.size())).toUcs4();
                for (auto ch : chars) {
                    const auto key = static_cast<std::uint32_t>(QChar::toUpper(ch));
                    enqueue_input([this, edge, key, ch]() {
                        const std::lock_guard<std::timed_mutex> guard(state_.lockdown);
                        edge(key, ch, true);
                        edge(key, ch, false);
                    });
                }
                return "OK queued " + std::to_string(chars.size());
            }
            std::istringstream words(arg);
            std::string name, action, extra;
            words >> name >> action >> extra;
            action = lowercase(action);
            if (!extra.empty() || (!action.empty() && action != "down" && action != "up"))
                return "ERR key <name> [down|up]";
            static const std::unordered_map<std::string, std::uint32_t> names = {
                {"enter", Qt::Key_Return}, {"return", Qt::Key_Return}, {"escape", Qt::Key_Escape},
                {"esc", Qt::Key_Escape}, {"tab", Qt::Key_Tab}, {"backspace", Qt::Key_Backspace},
                {"delete", Qt::Key_Delete}, {"space", Qt::Key_Space}, {"left", Qt::Key_Left},
                {"right", Qt::Key_Right}, {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
                {"home", Qt::Key_Home}, {"end", Qt::Key_End}, {"pageup", Qt::Key_PageUp},
                {"pagedown", Qt::Key_PageDown}, {"shift", Qt::Key_Shift}, {"ctrl", Qt::Key_Control},
                {"control", Qt::Key_Control}, {"alt", Qt::Key_Alt}, {"chr", Qt::Key_Alt},
                {"menu", Qt::Key_Menu}
            };
            const auto lower = lowercase(name);
            std::uint32_t code = 0, text = 0;
            if (auto found = names.find(lower); found != names.end()) code = found->second;
            else if (std::regex_match(lower, std::regex("f([1-9]|1[0-2])"))) code = Qt::Key_F1 + std::stoi(lower.substr(1)) - 1;
            else {
                auto chars = QString::fromUtf8(name.c_str()).toUcs4();
                if (chars.size() != 1) return "ERR unknown key";
                // key is a physical key; type supplies Unicode text and modifiers.
                code = QChar::toUpper(chars[0]);
            }
            enqueue_input([this, edge, code, text, action]() {
                const std::lock_guard<std::timed_mutex> guard(state_.lockdown);
                if (action != "up") edge(code, text, true);
                if (action != "down") edge(code, text, false);
            });
            return "OK queued key";
        }

        if (verb == "apps") {
            kernel_system *kern = state_.symsys ? state_.symsys->get_kernel_system() : nullptr;
            if (!kern) {
                return "ERR no kernel";
            }

            const std::lock_guard<kernel_system> guard(*kern);
            applist_server *svr = get_applist_server(state_);

            if (!svr) {
                return "ERR no app list server";
            }

            std::string json = "[";
            for (apa_app_registry &reg : svr->get_registerations()) {
                if (json.size() > 1) {
                    json += ",";
                }

                json += "{\"uid\":\"" + hex_uid(reg.mandatory_info.uid) + "\",\"name\":\"" + json_escape(caption_of(reg, true))
                    + "\",\"short\":\"" + json_escape(caption_of(reg, false)) + "\"}";
            }

            return "OK " + json + "]";
        }

        if (verb == "list" || verb == "focus") {
            auto *kern = state_.symsys ? state_.symsys->get_kernel_system() : nullptr;
            if (kern) {
                const std::lock_guard<kernel_system> guard(*kern);
                auto *bridge = get_rom_window_bridge(kern);
                if (bridge && !bridge->snapshot.ready)
                    return "ERR ROM window bridge not ready (install the current SysState.exe)";
            }
        }

        if (verb == "list") {
            const std::vector<running_app> apps = list_running_apps(state_, lowercase(arg) == "all");
            std::string json = "[";

            for (const running_app &app : apps) {
                if (json.size() > 1) {
                    json += ",";
                }

                json += app_json(app);
            }

            return "OK " + json + "]";
        }

        if (verb == "focus") {
            for (const running_app &app : list_running_apps(state_, true)) {
                if (app.focus) {
                    return "OK " + app_json(app);
                }
            }

            return "OK null";
        }

        if (verb == "launch") {
            if (arg.empty()) {
                return "ERR launch needs an app UID or caption";
            }

            std::string spec;
            std::string document;
            split_app_and_document(state_, arg, spec, document);

            running_app app;
            if (!launch_app(state_, spec, state_.kiosk.enabled, &app, &err, document)) {
                return "ERR " + err;
            }

            return "OK launched uid=" + hex_uid(app.uid) + " name=\"" + json_escape(app.name) + "\""
                + (document.empty() ? std::string() : " document=\"" + json_escape(document) + "\"");
        }

        if (verb == "switch") {
            if (arg.empty()) {
                return "ERR switch needs an app UID or caption";
            }

            running_app app;
            switch (switch_to_app(state_, arg, &app, &err)) {
            case switch_result::queued:
                return "OK queued uid=" + hex_uid(app.uid) + " wg=" + std::to_string(app.window_group);

            case switch_result::switched:
                return "OK switched uid=" + hex_uid(app.uid) + " name=\"" + json_escape(app.name) + "\" wg="
                    + std::to_string(app.window_group) + " ordinal=" + std::to_string(app.ordinal) + " focus="
                    + (app.focus ? "1" : "0");

            case switch_result::launched:
                return "OK launched uid=" + hex_uid(app.uid) + " name=\"" + json_escape(app.name) + "\"";

            default:
                return "ERR " + err;
            }
        }

        if (verb == "screenshot") {
            const std::size_t split = arg.find_last_of(" \t");
            std::string path = arg;
            bool native = false;

            if ((split != std::string::npos) && (lowercase(trim(arg.substr(split + 1))) == "native")) {
                native = true;
                path = trim(arg.substr(0, split));
            }

            if (path.empty()) {
                return "ERR screenshot needs a path";
            }

            if (!state_.ui_main) {
                return "ERR no window";
            }

            std::string detail;
            if (!state_.ui_main->museum_screenshot(resolve_launch_path(path), native, &detail)) {
                return "ERR " + detail;
            }

            return "OK " + detail;
        }

        if (verb == "stats") {
            std::uint64_t guest_fps = 0;
            if (state_.ui_main) {
                guest_fps = state_.ui_main->museum_guest_fps();
            }

            return "OK frames=" + std::to_string(state_.frames_presented.load()) + " fps=" + std::to_string(guest_fps)
                + " view=" + std::to_string(state_.view_width.load()) + "x" + std::to_string(state_.view_height.load()) + "+"
                + std::to_string(state_.view_x.load()) + "+" + std::to_string(state_.view_y.load()) + " "
                + screen_diagnostics(state_);
        }

        if (verb == "refresh") {
            if (!refresh_screen(state_, &err)) {
                return "ERR " + err;
            }

            return "OK refresh";
        }

        if (verb == "reset") {
            input_timer_->stop();
            input_commands_.clear();
            if (!state_.ui_main) {
                return "ERR no window";
            }

            if (!state_.ui_main->museum_reset(&err)) {
                return "ERR " + err;
            }

            return "OK reset";
        }

        if (verb == "quit") {
            input_timer_->stop();
            input_commands_.clear();
            quit_after = true;
            return "OK bye";
        }

        return "ERR unknown verb '" + verb + "' (try help)";
    }
}
