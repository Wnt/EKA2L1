/*
 * Copyright (c) 2021 EKA2L1 Team.
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

#include <kernel/legacy/mutex.h>
#include <kernel/kernel.h>

#include <common/log.h>

#include <cstdlib>
#include <vector>

namespace eka2l1::kernel::legacy {
    mutex::mutex(kernel_system *kern, const std::string mut_name, kernel::access_type access)
        : sync_object_base(kern, mut_name, 1, access)
        , holding_(nullptr)
        , hold_count_(0) {
        obj_type = kernel::object_type::mutex;
    }

    // EKA1 RMutex semantics: a free mutex is taken at once; a busy one blocks the caller until the holder's
    // last Signal hands it over, and the woken waiter IS the new holder (it may nest at once). The original
    // code never made the woken waiter the holder, and recorded a caller as the holder whenever the holder
    // field was empty, even when the mutex was in fact taken and the caller was about to block. After one
    // contended hand-over, a nested Wait by the new owner therefore blocked on its own mutex, and every
    // later caller queued behind it: three Series 80 skin loaders loading bitmaps at once all ended waiting
    // on FbsLargeBitmapAccess (count -3, nobody left to signal). EKA2L1_FIX_MUTEX_HANDOFF=0 restores the
    // old behaviour.
    static bool mutex_handoff_fix() {
        static const bool on = []() {
            const char *v = std::getenv("EKA2L1_FIX_MUTEX_HANDOFF");
            return !v || (v[0] != '0');
        }();
        return on;
    }

    void mutex::wait() {
        kernel::thread *crr_thread = kern->crr_thread();

        if (mutex_handoff_fix()) {
            if (holding_ == crr_thread) {
                hold_count_++;
                return;
            }

            if (count() > 0) {
                holding_ = crr_thread;
                hold_count_ = 1;
            } else {
                LOG_TRACE(KERNEL, "{} waits for mutex {} (holder {}, holds {}, count {})", crr_thread->name(), name(),
                    holding_ ? holding_->name() : std::string("none"), hold_count_, count());
            }

            wait_impl(thread_state::wait_mutex);
            return;
        }

        if (!holding_) {
            holding_ = crr_thread;
            hold_count_++;
        } else {
            if (holding_ == crr_thread) {
                hold_count_++;
                return;
            }
        }

        if (count() <= 0) {
            LOG_TRACE(KERNEL, "{} waits for mutex {} (holder {}, holds {}, count {})", crr_thread->name(), name(),
                holding_ ? holding_->name() : std::string("none"), hold_count_, count());
        }

        wait_impl(thread_state::wait_mutex);
    }

    void mutex::signal() {
        kernel::thread *crr_thread = kern->crr_thread();
        if (holding_ == crr_thread) {
            if (--hold_count_ == 0) {
                holding_ = nullptr;
            } else {
                return;
            }
        }

        if (count() < 0) {
            LOG_TRACE(KERNEL, "{} signals contended mutex {} (holder now {}, count {})", crr_thread->name(), name(),
                holding_ ? holding_->name() : std::string("none"), count());

            if (mutex_handoff_fix()) {
                // signal_impl wakes the first waiter; whichever waiter stops waiting on us is the new holder.
                std::vector<kernel::thread *> waiting;

                for (auto &obj : kern->get_thread_list()) {
                    kernel::thread *thr = reinterpret_cast<kernel::thread *>(obj.get());

                    if (thr && (thr->wait_obj == this) && (thr->current_state() == thread_state::wait_mutex)) {
                        waiting.push_back(thr);
                    }
                }

                signal_impl(1);

                for (kernel::thread *thr : waiting) {
                    if (thr->wait_obj != this) {
                        holding_ = thr;
                        hold_count_ = 1;
                        break;
                    }
                }

                return;
            }
        }

        signal_impl(1);
    }

    bool mutex::suspend_waiting_thread(thread *thr) {
        return suspend_waiting_thread_impl(thr, thread_state::wait_mutex_suspend);
    }

    bool mutex::unsuspend_waiting_thread(thread *thr) {
        return unsuspend_waiting_thread_impl(thr, thread_state::wait_mutex);
    }
}