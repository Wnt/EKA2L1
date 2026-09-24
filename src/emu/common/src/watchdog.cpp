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

#include <common/log.h>
#include <common/thread.h>
#include <common/watchdog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace eka2l1::common {
    namespace {
        using clock = std::chrono::steady_clock;

        constexpr std::chrono::seconds WATCHDOG_INTERVAL{ 1 };
        constexpr std::chrono::seconds WATCHDOG_STALL_THRESHOLD{ 5 };
        constexpr std::chrono::seconds WATCHDOG_REPEAT_REPORT{ 30 };
        constexpr const char *watchdog_thread_name = "Watchdog";
    }

    struct watch_record {
        const char *name_;

        // Written by the watched thread, read by the watchdog.
        std::atomic<std::uint64_t> beats_{ 0 };
        std::atomic<const char *> parked_on_{ nullptr };
        std::atomic<const char *> waiting_for_{ nullptr };

        // The watchdog alone touches these.
        std::uint64_t last_beats_ = 0;
        clock::time_point last_progress_ = clock::now();
        bool stalled_ = false;
    };

    namespace {
        // Leaked on purpose: watched threads may outlive static destruction.
        std::mutex &records_lock() {
            static std::mutex *lock = new std::mutex();
            return *lock;
        }

        std::vector<std::shared_ptr<watch_record>> &records() {
            static auto *all = new std::vector<std::shared_ptr<watch_record>>();
            return *all;
        }

        thread_local std::shared_ptr<watch_record> this_thread_record;

        std::mutex watchdog_lock;
        std::condition_variable watchdog_wake;
        std::thread watchdog_thread;
        bool watchdog_stopping = false;

        std::int64_t seconds_since(const clock::time_point when) {
            return std::chrono::duration_cast<std::chrono::seconds>(clock::now() - when).count();
        }

        bool freeze_reported = false;
        clock::time_point freeze_began;
        clock::time_point next_repeat;

        // All the emulator can say about itself when no stuck thread can be asked.
        void report_freeze(const std::vector<std::shared_ptr<watch_record>> &all) {
            LOG_WARN(WATCHDOG, "The emulator has not made progress for {} s; thread states:",
                seconds_since(freeze_began));

            for (const std::shared_ptr<watch_record> &record : all) {
                const char *parked = record->parked_on_.load();
                const char *waiting = record->waiting_for_.load();

                if (parked) {
                    LOG_WARN(WATCHDOG, "  {}: parked on {}", record->name_, parked);
                } else if (waiting) {
                    LOG_WARN(WATCHDOG, "  {}: stuck {} s, waiting for {}", record->name_,
                        seconds_since(record->last_progress_), waiting);
                } else if (record->stalled_) {
                    LOG_WARN(WATCHDOG, "  {}: stuck {} s, not in a known wait", record->name_,
                        seconds_since(record->last_progress_));
                } else {
                    LOG_WARN(WATCHDOG, "  {}: running", record->name_);
                }
            }
        }

        void check_once() {
            std::vector<std::shared_ptr<watch_record>> all;

            {
                const std::lock_guard<std::mutex> guard(records_lock());
                all = records();
            }

            bool any_stalled = false;

            for (const std::shared_ptr<watch_record> &record : all) {
                const std::uint64_t beats = record->beats_.load();

                if ((record->parked_on_.load() != nullptr) || (beats != record->last_beats_)) {
                    record->last_beats_ = beats;
                    record->last_progress_ = clock::now();
                    record->stalled_ = false;

                    continue;
                }

                record->stalled_ = (clock::now() - record->last_progress_ >= WATCHDOG_STALL_THRESHOLD);
                any_stalled = any_stalled || record->stalled_;
            }

            // One table per freeze: a freeze jams every thread, and a report each would
            // repeat itself.
            if (any_stalled) {
                if (!freeze_reported) {
                    freeze_reported = true;
                    freeze_began = clock::now() - WATCHDOG_STALL_THRESHOLD;
                    next_repeat = clock::now() + WATCHDOG_REPEAT_REPORT;

                    report_freeze(all);
                } else if (clock::now() >= next_repeat) {
                    next_repeat = clock::now() + WATCHDOG_REPEAT_REPORT;
                    report_freeze(all);
                }
            } else if (freeze_reported) {
                freeze_reported = false;
                LOG_WARN(WATCHDOG, "The emulator resumed after {} s", seconds_since(freeze_began));
            }
        }

        void watchdog_loop() {
            set_thread_name(watchdog_thread_name);

            std::unique_lock<std::mutex> lock(watchdog_lock);

            while (!watchdog_stopping) {
                watchdog_wake.wait_for(lock, WATCHDOG_INTERVAL, []() { return watchdog_stopping; });

                if (watchdog_stopping) {
                    break;
                }

                lock.unlock();
                check_once();
                lock.lock();
            }
        }
    }

    watched_thread::watched_thread(const char *name) {
        this_thread_record = std::make_shared<watch_record>();
        this_thread_record->name_ = name;
        record_ = this_thread_record.get();

        const std::lock_guard<std::mutex> guard(records_lock());
        records().push_back(this_thread_record);
    }

    watched_thread::~watched_thread() {
        {
            const std::lock_guard<std::mutex> guard(records_lock());
            std::vector<std::shared_ptr<watch_record>> &all = records();

            all.erase(std::remove(all.begin(), all.end(), this_thread_record), all.end());
        }

        this_thread_record.reset();
        record_ = nullptr;
    }

    void watched_thread::beat() {
        record_->beats_.fetch_add(1, std::memory_order_relaxed);
    }

    parked_scope::parked_scope(const char *why)
        : previous_(nullptr) {
        if (this_thread_record) {
            previous_ = this_thread_record->parked_on_.exchange(why);
        }
    }

    parked_scope::~parked_scope() {
        if (this_thread_record) {
            this_thread_record->parked_on_.store(previous_);
        }
    }

    void publish_wait(const char *what) {
        if (this_thread_record) {
            this_thread_record->waiting_for_.store(what);
        }
    }

    void start_watchdog() {
        const std::lock_guard<std::mutex> guard(watchdog_lock);

        if (watchdog_thread.joinable()) {
            return;
        }

        watchdog_stopping = false;
        watchdog_thread = std::thread(watchdog_loop);
    }

    void stop_watchdog() {
        {
            const std::lock_guard<std::mutex> guard(watchdog_lock);

            if (!watchdog_thread.joinable()) {
                return;
            }

            watchdog_stopping = true;
        }

        watchdog_wake.notify_all();
        watchdog_thread.join();
    }
}
