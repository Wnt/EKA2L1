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

#pragma once

// Museum-station frontend: the kiosk presentation's command-line options and the ekactl/1
// control socket (launch, switch, list, screenshot, reset, quit). The options document
// themselves in --help; the protocol is described on control_server below.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QLocalServer;
class QLocalSocket;

namespace eka2l1 {
    class applist_server;
    struct apa_app_registry;

    namespace common {
        class arg_parser;
    }
}

namespace eka2l1::desktop {
    struct emulator;

    /**
     * \brief An app with a window group on the screen, front first.
     */
    struct running_app {
        std::uint32_t uid = 0;          ///< From the window group name, else the process's UID3.
        std::string name;               ///< Caption from the window group name.
        std::uint32_t window_group = 0; ///< Window server identifier of the group.
        int ordinal = 0;                ///< Position among all window groups, 0 = front.
        bool focus = false;             ///< The group has the keyboard focus.
        std::uint64_t process = 0;      ///< Kernel object id of the owning process.
        bool is_app = true;             ///< False for a plain (non-apparc) group, listed by "list all".
    };

    /**
     * \brief Remember where the process was started, before main() moves into the data directory.
     */
    void set_launch_directory(const std::string &dir);

    /**
     * \brief Absolute paths pass through; relative ones resolve against the launch directory.
     */
    std::string resolve_launch_path(const std::string &path);

    /**
     * \brief Pick up the options the logger needs before stage one runs (--log-file, --log-filter,
     *        --no-console-log). The regular parser then only consumes them.
     */
    void prescan_early_options(emulator &state, const int argc, const char **argv);

    /**
     * \brief Register --kiosk*, --control-socket, --log-*, --no-console-log and --no-update-check.
     */
    void register_museum_options(common::arg_parser &parser);

    applist_server *get_applist_server(emulator &state);

    /**
     * \brief Find an installed app. "0x..." (or a plain decimal number) is a UID; anything else is a
     *        caption, matched exactly against the long then the short caption, then without case.
     */
    apa_app_registry *find_app_registry(applist_server *svr, const std::string &spec, std::string *err);

    /**
     * \brief Start an app through the app list server, under the kernel lock.
     *
     * With track_exit, the app's exit reaches main_window::on_app_exited (kiosk: relaunch home or quit).
     * Must not be called from a thread that holds the kernel lock.
     */
    bool launch_app(emulator &state, const std::string &spec, const bool track_exit, running_app *launched,
        std::string *err, const std::string &document = std::string());

    /**
     * \brief "Web http://example.com/" -> app "Web", document "http://example.com/". A whole argument
     *        that names an app (a caption with spaces) is taken as the app.
     */
    void split_app_and_document(emulator &state, const std::string &arg, std::string &spec, std::string &document);

    /**
     * \brief The window groups on the active screen, front first, under the kernel lock.
     */
    std::vector<running_app> list_running_apps(emulator &state, const bool include_plain_groups);

    enum class switch_result {
        failed,
        switched,   ///< The app's window group went to ordinal position 0.
        launched    ///< The app was not running and has been started.
    };

    /**
     * \brief Bring an app's window group to the front, starting the app if it is not running.
     */
    switch_result switch_to_app(emulator &state, const std::string &spec, running_app *result, std::string *err);

    /**
     * \brief Recompose the active screen from every window's stored drawing, as an ordinal change
     *        does: visible regions recalculated, the whole screen redrawn and presented.
     */
    bool refresh_screen(emulator &state, std::string *err);

    /**
     * \brief One line of screen internals for the stats verb (texture, scale, flags).
     */
    std::string screen_diagnostics(emulator &state);

    /**
     * \brief The ekactl/1 line protocol on a Unix-domain socket, served on the GUI thread.
     *
     * On connect the server sends one line:
     *   HELLO ekactl/1 device=<firmware code> screen=<W>x<H> view=<W>x<H>+<X>+<Y> [scale=<N>] window=<W>x<H>+<X>+<Y>
     * (view: where the emulated screen sits inside the display window). Every command is one
     * line and gets exactly one line back, "OK[ <payload>]" or "ERR <reason>":
     *   ping                       OK pong
     *   help                       OK verbs: ...
     *   apps                       OK [{"uid":"0x...","name":"...","short":"..."},...]   installed apps
     *   list [all]                 OK [{"uid","name","wg","ordinal","focus","app"},...]  window groups,
     *                              front first ("all" adds groups that are not apparc apps)
     *   focus                      OK {...} | OK null                                   the focused group
     *   launch <uid|caption> [doc] OK launched uid=0x... name="..."                     always a new instance;
     *                              a document (a file, or a URL for Web) opens with EApaCommandOpen
     *   switch <uid|caption>       OK switched uid=... name="..." wg=N ordinal=N focus=0|1
     *                              | OK launched ...                                    front, or start it
     *   screenshot <path> [native] OK <path> <W>x<H>        the display window, or the screen at 1:1
     *   stats                      OK frames=N fps=N view=WxH+X+Y ...  presents since start, guest fps
     *   refresh                    OK refresh      recompose the screen from the windows' stored drawing
     *   reset                      OK reset        in-process system reset, then the home/--run app
     *   quit                       OK bye          then the process exits with code 0
     */
    class control_server {
    public:
        explicit control_server(emulator &state);
        ~control_server();

        bool listen(const std::string &path, std::string *err);

    private:
        emulator &state_;
        QLocalServer *server_;
        std::string path_;
        std::map<QLocalSocket *, std::string> pending_;

        void on_new_connection();
        void on_ready_read(QLocalSocket *socket);
        std::string hello_line();
        std::string execute(const std::string &line, bool &quit_after);
    };
}
