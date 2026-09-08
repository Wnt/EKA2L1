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

#include <common/log.h>

#include <chrono>
#include <cstdint>

namespace eka2l1::common {
    // A wait that has run this long is no longer ordinary scheduling. A freeze is
    // reported as "it stopped responding", which is seconds of nothing at all;
    // complaining any sooner would bury the real thing under routine contention.
    constexpr std::uint64_t STALL_FIRST_REPORT_US = 3000000;

    // How often to say so again while the wait is still stuck. A frozen emulator
    // should keep reporting -- that is how it is told apart from a dead one -- but
    // not every three seconds for as long as the window is left open.
    constexpr std::uint64_t STALL_REPEAT_REPORT_US = 15000000;

    /**
     * \brief What the calling thread is blocked on, or nullptr if it is not.
     *
     * Set for the duration of any wait_reporting_stall, and by wait_scope directly for
     * waits that cannot be given a timeout. Lets an observer say what a thread was
     * doing rather than only that it stopped.
     */
    const char *current_wait();

    /**
     * \brief Record what the calling thread is blocked on for as long as this lives.
     *
     * Nests: the previous description is restored on destruction.
     */
    class wait_scope {
    private:
        const char *previous_;

    public:
        explicit wait_scope(const char *what);
        ~wait_scope();

        wait_scope(const wait_scope &) = delete;
        wait_scope &operator=(const wait_scope &) = delete;
    };

    /**
     * \brief Wait for something, and say so in the log if the wait does not end.
     *
     * \param cls          Log class to report under.
     * \param what         What is waited for, phrased to follow "Waiting for ...".
     * \param try_wait_for Waits up to the given number of microseconds, returning true
     *                     once the thing waited for has happened and false on timeout.
     *
     * The wait is never abandoned and no behaviour changes: a timeout only produces a
     * log line. That is the whole point -- it turns a freeze from output that simply
     * stops into output that names the wait it stopped in, and reports again if the
     * wait ever completes.
     */
    template <typename WaitFor>
    void wait_reporting_stall(const log_class cls, const char *what, WaitFor &&try_wait_for) {
        const wait_scope scope(what);

        if (try_wait_for(STALL_FIRST_REPORT_US)) {
            return;
        }

        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();

        const auto waited_ms = [&began]() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - began)
                       .count()
                + static_cast<std::int64_t>(STALL_FIRST_REPORT_US / 1000);
        };

        do {
            LOG_WARN(cls, "Waiting for {} has taken {} ms and has not finished; the emulator "
                          "is not responding", what, waited_ms());
        } while (!try_wait_for(STALL_REPEAT_REPORT_US));

        LOG_WARN(cls, "Waiting for {} finished after {} ms", what, waited_ms());
    }
}
