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

#include <kernel/ldd.h>

namespace eka2l1::ldd {
    /**
     * @brief A device the emulator does not model, that opens anyway.
     *
     * Some drivers are not the emulator's business: the sound hardware is
     * driven by the audio driver rather than by the guest's own device, and
     * nothing the guest asks the device would change what comes out. What
     * matters is that the device OPENS. A media server that cannot open its
     * audio device does not report the failure - it dereferences the channel
     * it did not get, takes itself down with it, and every application waiting
     * on sound waits forever.
     *
     * So this accepts, does nothing, and says so in the log.
     */
    class null_device_channel : public channel {
    public:
        explicit null_device_channel(kernel_system *kern, system *sys, epoc::version ver);

        std::int32_t do_control(kernel::thread *r, const std::uint32_t n, const eka2l1::ptr<void> arg1,
            const eka2l1::ptr<void> arg2) override;

        std::int32_t do_request(epoc::notify_info info, const std::uint32_t n, const eka2l1::ptr<void> arg1,
            const eka2l1::ptr<void> arg2, const bool is_supervisor) override;
    };

    class null_device_factory : public factory {
    public:
        explicit null_device_factory(kernel_system *kern, system *sys);

        void install() override;
        std::unique_ptr<channel> make_channel(epoc::version ver) override;
    };
}
