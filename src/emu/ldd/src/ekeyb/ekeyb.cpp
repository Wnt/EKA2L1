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

#include <ldd/ekeyb/ekeyb.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/svc.h>

#include <common/log.h>
#include <utils/err.h>
#include <services/window/window.h>

#include <string>
#include <system/epoc.h>
#include <config/config.h>

namespace eka2l1::ldd {
    static const std::string EKEYB_FACTORY_NAME = "EKeyB";

    ekeyb_factory::ekeyb_factory(kernel_system *kern, system *sys)
        : factory(kern, sys) {
    }

    void ekeyb_factory::install() {
        obj_name = kern->rom_raw_input_enabled() ? "EKeyb" : EKEYB_FACTORY_NAME;
    }

    std::unique_ptr<channel> ekeyb_factory::make_channel(epoc::version ver) {
        return std::make_unique<ekeyb_channel>(kern, sys_, ver);
    }

    ekeyb_channel::ekeyb_channel(kernel_system *kern, system *sys, epoc::version ver)
        : channel(kern, sys, ver) {
    }

    std::int32_t ekeyb_channel::do_control(kernel::thread *r, const std::uint32_t n, const eka2l1::ptr<void> arg1,
        const eka2l1::ptr<void> arg2) {
        if (kern->rom_raw_input_enabled()) {
            LOG_INFO(LDD_MMCIF, "ROM EKeyb control {} arg1=0x{:X} arg2=0x{:X}", n, arg1.ptr_address(), arg2.ptr_address());
            // ROM HAL jump table at 0x500434c8: control 0 = case state,
            // 2 = keyboard index (HAL 69), 5/6 = mouse speed/acceleration.
            // All getters take TInt* in arg1.
            auto *value = reinterpret_cast<std::int32_t *>(arg1.get(r->owning_process()));
            if (n == 0 || n == 2 || n == 5 || n == 6) {
                if (!value) return epoc::error_argument;
                if (n == 2) *value = sys_->get_config()->keyboard_layout_index;
                else if (n == 0) *value = 0; // RAE-6 HAL default case state
                else *value = 1; // RAE-6 HAL defaults for mouse speed/acceleration
                return epoc::error_none;
            }
            return epoc::error_not_supported;
        }
        LOG_TRACE(LDD_MMCIF, "Unimplemented ekeyb control opcode {}", n);
        return 0;
    }

    std::int32_t ekeyb_channel::do_request(epoc::notify_info info, const std::uint32_t n,
        const eka2l1::ptr<void> arg1, const eka2l1::ptr<void> arg2,
        const bool is_supervisor) {
        if (kern->rom_raw_input_enabled()) {
            info.complete(epoc::error_not_supported);
            return epoc::error_none;
        }
        LOG_TRACE(LDD_MMCIF, "Unimplemented ekeyb request opcode {}", n);
        return 0;
    }
}