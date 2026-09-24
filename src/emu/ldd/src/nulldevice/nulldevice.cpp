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

#include <ldd/nulldevice/nulldevice.h>

#include <common/log.h>
#include <utils/err.h>

#include <string>

namespace eka2l1::ldd {
    static const std::string NULL_DEVICE_FACTORY_NAME = "NullDevice";

    null_device_factory::null_device_factory(kernel_system *kern, system *sys)
        : factory(kern, sys) {
    }

    void null_device_factory::install() {
        obj_name = NULL_DEVICE_FACTORY_NAME;
    }

    std::unique_ptr<channel> null_device_factory::make_channel(epoc::version ver) {
        return std::make_unique<null_device_channel>(kern, sys_, ver);
    }

    null_device_channel::null_device_channel(kernel_system *kern, system *sys, epoc::version ver)
        : channel(kern, sys, ver) {
    }

    std::int32_t null_device_channel::do_control(kernel::thread *r, const std::uint32_t n,
        const eka2l1::ptr<void> arg1, const eka2l1::ptr<void> arg2) {
        LOG_TRACE(LDD_MMCIF, "Null device control opcode {} accepted and ignored", n);
        return epoc::error_none;
    }

    std::int32_t null_device_channel::do_request(epoc::notify_info info, const std::uint32_t n,
        const eka2l1::ptr<void> arg1, const eka2l1::ptr<void> arg2, const bool is_supervisor) {
        LOG_TRACE(LDD_MMCIF, "Null device request opcode {} accepted and ignored", n);

        // A request nobody will complete is a thread that waits forever.
        info.complete(epoc::error_none);
        return epoc::error_none;
    }
}
