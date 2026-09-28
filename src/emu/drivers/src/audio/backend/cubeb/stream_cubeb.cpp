/*
 * Copyright (c) 2020 EKA2L1 Team.
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

#include <drivers/audio/backend/cubeb/stream_cubeb.h>
#include <drivers/audio/audio.h>

#include <common/algorithm.h>
#include <common/log.h>
#include <common/platform.h>

#include <chrono>

namespace eka2l1::drivers {
    static long data_callback_redirector(cubeb_stream *stm, void *user,
        const void *input_buffer, void *output_buffer, long nframes) {
        cubeb_audio_stream_base *stream = reinterpret_cast<cubeb_audio_stream_base *>(user);
        return static_cast<long>(stream->call_callback(input_buffer ? 
            reinterpret_cast<std::int16_t *>(const_cast<void*>(input_buffer))
            : reinterpret_cast<std::int16_t *>(output_buffer), nframes));
    }

    void state_callback_redirector(cubeb_stream *stream, void *user_data, cubeb_state state) {
    }

    cubeb_audio_stream_base::cubeb_audio_stream_base(cubeb *context, const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback, bool is_recording) 
        : stream_(nullptr)
        , callback_(callback)
        , idled_frames_(0)
        , internal_channels_(channels)
        , in_action_(false)
        , sample_rate_(sample_rate) {
        if (!context) {
            LOG_WARN(DRIVER_AUD, "No audio device: this stream plays into a null sink (silent, real time)");
            return;
        }

        cubeb_stream_params params;
        params.format = CUBEB_SAMPLE_S16LE;
        params.rate = sample_rate;
        params.channels = channels;
        params.layout = (channels == 1) ? CUBEB_LAYOUT_MONO : CUBEB_LAYOUT_STEREO;
        params.prefs = CUBEB_STREAM_PREF_NONE;

        std::uint32_t minimum_latency;
#ifdef EKA2L1_PLATFORM_ANDROID
        minimum_latency = 256;
#else
        minimum_latency = 100 * sample_rate / 1000; // Firefox default

        if (cubeb_get_min_latency(context, &params, &minimum_latency) != CUBEB_OK) {
            LOG_ERROR(DRIVER_AUD, "Error trying to get minimum latency. Use default");
        }
#endif

#ifdef EKA2L1_PLATFORM_ANDROID
        minimum_latency = common::max<std::uint32_t>(256U, minimum_latency);
#endif

        const auto result = cubeb_stream_init(context, &stream_, "EKA2L1 StreamPeam",
            nullptr, is_recording ? &params : nullptr, nullptr, is_recording ? nullptr : &params, minimum_latency,
            data_callback_redirector, state_callback_redirector, this);

        if (result != CUBEB_OK) {
            LOG_CRITICAL(DRIVER_AUD, "Error trying to initialize cubeb stream! Using a null sink (silent, real time)");
            stream_ = nullptr;
            return;
        }
    }

    void cubeb_audio_stream_base::null_sink_loop() {
        static constexpr std::uint32_t TICK_MS = 10;
        std::vector<std::int16_t> scratch;
        const auto begin = std::chrono::steady_clock::now();
        std::uint64_t pulled = 0;

        while (null_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));

            const std::uint64_t elapsed_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin).count());
            const std::uint64_t due = elapsed_us * sample_rate_ / 1000000ULL;

            if (due <= pulled) {
                continue;
            }

            const long frames = static_cast<long>(common::min<std::uint64_t>(due - pulled, sample_rate_));
            scratch.assign(static_cast<std::size_t>(frames) * internal_channels_, 0);

            call_callback(scratch.data(), frames);

            pulled += static_cast<std::uint64_t>(frames);
            null_frames_ += static_cast<std::uint64_t>(frames);
        }
    }

    cubeb_audio_stream_base::~cubeb_audio_stream_base() {
        null_running_ = false;
        if (null_thread_.joinable()) {
            null_thread_.join();
        }

        if (stream_) {
            cubeb_stream_destroy(stream_);
        }
    }

    std::size_t cubeb_audio_stream_base::call_callback(std::int16_t *output_buffer, const long frames) {
        if (should_stream_idle()) {
            std::memset(output_buffer, 0, frames * internal_channels_ * sizeof(std::int16_t));
            idled_frames_ += static_cast<std::uint64_t>(frames);
  
            return static_cast<std::size_t>(frames);
        }

        return callback_(output_buffer, frames);
    }
    
    bool cubeb_audio_stream_base::current_frame_position_impl(std::uint64_t *pos) {
        if (!stream_) {
            *pos = null_frames_;
        } else if (cubeb_stream_get_position(stream_, pos) != CUBEB_OK) {
            return false;
        }

        if (idled_frames_ >= *pos) {
            *pos = 0;
        } else {
            *pos -= idled_frames_;
        }

        return true;
    }

    bool cubeb_audio_stream_base::start_impl() {
        if (in_action_) {
            return true;
        }

        if (!stream_) {
            idled_frames_ = 0;
            null_frames_ = 0;
            null_running_ = true;
            null_thread_ = std::thread([this]() { null_sink_loop(); });
            in_action_ = true;

            return true;
        }

        if (cubeb_stream_start(stream_) == CUBEB_OK) {
            in_action_ = true;
            idled_frames_ = 0;

            return true;
        }

        return false;
    }

    bool cubeb_audio_stream_base::stop_impl() {
        if (!in_action_) {
            return true;
        }

        if (!stream_) {
            null_running_ = false;
            if (null_thread_.joinable() && (null_thread_.get_id() != std::this_thread::get_id())) {
                null_thread_.join();
            } else if (null_thread_.joinable()) {
                null_thread_.detach();
            }

            in_action_ = false;
            return true;
        }

        if (cubeb_stream_stop(stream_) == CUBEB_OK) {
            in_action_ = false;
            return true;
        }

        return false;
    }

    cubeb_audio_output_stream::cubeb_audio_output_stream(audio_driver *driver, cubeb *context, const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback)
        : audio_output_stream(driver, sample_rate, channels)
        , cubeb_audio_stream_base(context, sample_rate, channels, callback, false)
        , pausing_(false)
        , volume_(1.0f) {
    }

    cubeb_audio_output_stream::~cubeb_audio_output_stream() {
        // The null sink's thread calls should_stream_idle(): stop it while this object is whole.
        stop_impl();
    }

    bool cubeb_audio_output_stream::should_stream_idle() {
        return (pausing_ || driver_->suspending());
    }

    bool cubeb_audio_output_stream::start() {
        if (pausing_) {
            pausing_ = false;
            return true;
        }

        return cubeb_audio_stream_base::start_impl();
    }

    bool cubeb_audio_output_stream::stop() {
        if (cubeb_audio_stream_base::stop_impl()) {
            pausing_ = false;
            return true;
        }

        return false;
    }

    void cubeb_audio_output_stream::pause() {
        pausing_ = true;
    }

    bool cubeb_audio_output_stream::is_playing() {
        return in_action_;
    }

    bool cubeb_audio_output_stream::is_pausing() {
        return pausing_;
    }

    bool cubeb_audio_output_stream::set_volume(const float volume) {
        if (!stream_) {
            volume_ = volume;
            return true;
        }

        if (cubeb_stream_set_volume(stream_, volume * static_cast<float>(driver_->master_volume() / 100.0f)) == CUBEB_OK) {
            volume_ = volume;
            return true;
        }

        return false;
    }

    float cubeb_audio_output_stream::get_volume() const {
        return volume_;
    }

    bool cubeb_audio_output_stream::current_frame_position(std::uint64_t *pos) {
        return current_frame_position_impl(pos);
    }

    cubeb_audio_input_stream::cubeb_audio_input_stream(audio_driver *driver, cubeb *context, const std::uint32_t sample_rate, const std::uint8_t channels,
        data_callback callback)
        : audio_input_stream(driver, sample_rate, channels)
        , cubeb_audio_stream_base(context, sample_rate, channels, callback, true) {
    }

    cubeb_audio_input_stream::~cubeb_audio_input_stream() {
        stop_impl();

    }

    bool cubeb_audio_input_stream::start() {
        return cubeb_audio_stream_base::start_impl();
    }

    bool cubeb_audio_input_stream::stop() {
        return cubeb_audio_stream_base::stop_impl();
    }

    bool cubeb_audio_input_stream::is_recording() {
        return in_action_;
    }

    bool cubeb_audio_input_stream::current_frame_position(std::uint64_t *pos) {
        return cubeb_audio_stream_base::current_frame_position_impl(pos);
    }

    bool cubeb_audio_input_stream::should_stream_idle() {
        return driver_->suspending();
    }
}
