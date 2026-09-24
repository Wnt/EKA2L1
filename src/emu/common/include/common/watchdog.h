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

#include <cstdint>

namespace eka2l1::common {
    struct watch_record;

    /**
     * \brief Watch the calling thread for as long as this lives.
     *
     * \param name  Must outlive the thread; pass a literal.
     */
    class watched_thread {
    private:
        watch_record *record_;

    public:
        explicit watched_thread(const char *name);
        ~watched_thread();

        watched_thread(const watched_thread &) = delete;
        watched_thread &operator=(const watched_thread &) = delete;

        /**
         * \brief Report progress. A watched thread that stops beating without being
         *        parked is what the watchdog reports as frozen.
         */
        void beat();
    };

    /**
     * \brief Mark the calling thread as blocked on purpose, so that waiting for the
     *        user to resume or for work to arrive is not mistaken for a freeze.
     */
    class parked_scope {
    private:
        const char *previous_;

    public:
        explicit parked_scope(const char *why);
        ~parked_scope();

        parked_scope(const parked_scope &) = delete;
        parked_scope &operator=(const parked_scope &) = delete;
    };

    /**
     * \brief Record what the calling thread is blocked on, for the watchdog to report.
     *
     * Called by wait_scope; nesting is the caller's business.
     */
    void publish_wait(const char *what);

    void start_watchdog();
    void stop_watchdog();
}
