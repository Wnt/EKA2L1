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

class QApplication;

namespace eka2l1::common {
    class arg_parser;
}

namespace eka2l1::desktop {
    struct emulator;

    /**
     * \brief Register every command-line option, so --help can print them all before anything boots.
     */
    void register_command_line_options(common::arg_parser &parser);

    /**
     * \brief Entry point to the graphics driver thread.
     * \param state Emulator state.
     */
    void graphics_driver_thread(emulator &state);

    /**
     * \brief Entry point to the UI thread.
     * \param state Emulator state.
     */
    void ui_thread(emulator &state);

    /**
     * \brief Entry point to thread emulate Symbian OS.
     *
     * This thread may spawn multiple small threads emulating cores in the future.
     *
     * \param state Emulator state.
     */
    void os_thread(emulator &state);

    /**
     * @brief Kill all emulator thread from running. Does not wait for them.
     * @param state The emulator state.
     */
    void kill_emulator(emulator &state);

    /**
     * @brief Entry to emulator.
     *
     * This function spawns all necessary threads.
     *
     * In addition, you can provide extra command line argument to configure the emulator!.
     * 
     * @param state State of the emulator.
     */
    int emulator_entry(QApplication &application, emulator &state, const int argc, const char **argv);
}
