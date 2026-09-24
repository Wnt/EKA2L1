/*
 * Copyright (c) 2019 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project
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

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include <common/queue.h>
#include <common/sync.h>
#include <config/app_settings.h>
#include <config/config.h>
#include <package/manager.h>
#include <system/epoc.h>

#include <drivers/audio/audio.h>
#include <drivers/graphics/emu_window.h>
#include <drivers/graphics/graphics.h>
#include <drivers/input/emu_controller.h>
#include <drivers/sensor/sensor.h>

namespace eka2l1 {
    namespace drivers {
        class graphics_driver;
        class audio_driver;
    }

    namespace kernel {
        class process;
    }

    class window_server;
}

class main_window;

namespace eka2l1::desktop {
    class control_server;

    /**
     * \brief Museum kiosk presentation (--kiosk and friends).
     *
     * The window carries nothing but the emulated screen: no menu bar, status bar, frame,
     * tray icon or dialog. The screen is rendered at its native resolution and magnified by
     * an integer factor with nearest-neighbour filtering, at a fixed place in a window of a
     * fixed size and position, so every launch has the same geometry.
     */
    struct kiosk_options {
        bool enabled = false;

        int scale = 2;                  ///< Integer magnification of the emulated screen.
        bool geometry_given = false;
        int window_x = 0;               ///< Window position on the X screen.
        int window_y = 0;
        int window_width = 0;           ///< 0: the magnified screen's width.
        int window_height = 0;          ///< 0: the magnified screen's height.
        int offset_x = -1;              ///< Screen position inside the window, -1: centred.
        int offset_y = -1;
        std::uint32_t background = 0xFF000000; ///< ARGB fill around the screen.

        std::string home_app;           ///< App relaunched when the command-line app exits.
    };

    /**
     * \brief State of the emulator on desktop.
     */
    struct emulator {
        std::unique_ptr<system> symsys;
        std::unique_ptr<drivers::graphics_driver> graphics_driver;
        std::unique_ptr<drivers::audio_driver> audio_driver;
        std::unique_ptr<drivers::sensor_driver> sensor_driver;
        std::unique_ptr<config::app_settings> app_settings;

        drivers::emu_window *window;
        drivers::emu_controller_ptr joystick_controller;

        std::atomic<bool> should_emu_quit;
        std::atomic<bool> should_emu_pause;
        std::atomic<bool> stage_two_inited;

        bool first_time;
        bool init_fullscreen;
        bool app_launch_from_command_line;
        bool inited_graphics;
        bool stretch_to_fill_display;

        common::event graphics_event;

        // init_event asks the OS thread to (re)attempt the stage two initialisation, init_done_event
        // reports an attempt back. They must stay separate: outside of Win32 common::event auto-resets
        // on wait, so one event for both directions lets the requester swallow its own signal.
        common::event init_event;
        common::event init_done_event;

        common::event pause_event;
        common::event kill_event;

        config::state conf;
        window_server *winserv;

        std::mutex lockdown;
        std::size_t sys_reset_cbh;

        main_window *ui_main;
        int present_status;

        std::string launched_app_name_;
        std::uint32_t launched_app_uid_ = 0;   ///< UID of the app --run started, 0 if a path.

        kiosk_options kiosk;

        std::string control_socket_path;        ///< --control-socket: ekactl/1 listener.
        control_server *control = nullptr;

        std::string log_file_path;              ///< --log-file: replaces EKA2L1.log in the data dir.
        std::string log_filter_override;        ///< --log-filter: replaces config.yml's log-filter.
        bool console_log = true;                ///< --no-console-log clears it.
        bool no_update_check = false;           ///< --no-update-check (implied by --kiosk and --run).

        std::atomic<std::uint64_t> frames_presented{ 0 }; ///< Screen redraws handed to the display.

        // Where the last present put the emulated screen inside the display widget, and the
        // screen's own size - what a screenshot crops and a client scales pointer coordinates by.
        std::atomic<int> view_x{ 0 };
        std::atomic<int> view_y{ 0 };
        std::atomic<int> view_width{ 0 };
        std::atomic<int> view_height{ 0 };
        std::atomic<int> screen_width{ 0 };
        std::atomic<int> screen_height{ 0 };

        std::string launch_spec_;               ///< What --run was given, relaunched by "reset".

        explicit emulator();

        void stage_one();
        bool stage_two();

        void on_system_reset(system *the_sys);
    };
}
