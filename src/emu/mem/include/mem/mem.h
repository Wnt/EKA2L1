/*
 * Copyright (c) 2019 EKA2L1 Team.
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

#include <mem/common.h>
#include <mem/chunk.h>
#include <mem/control.h>
#include <mem/mmu.h>
#include <mem/page.h>

#include <array>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace eka2l1 {
    class system;

    namespace mem {
        class mmu_base;
        class control_base;

        using control_impl = std::unique_ptr<control_base>;

        struct page_table_allocator;
        using page_table_allocator_impl = std::unique_ptr<page_table_allocator>;
    }

    namespace arm {
        class core;
    }

    namespace config {
        struct state;
    }

    class memory_system {
        friend class system;

        mem::control_impl impl_;
        mem::page_table_allocator_impl alloc_;

        void *rom_map_{};
        std::size_t rom_size_{};

        mem::vm_address rom_addr_{};
        config::state *conf_;

        struct rom_data_block {
            std::vector<std::uint8_t> bytes;
            mem::mem_model_chunk_impl chunk;
            std::size_t used{};
        };
        std::vector<std::unique_ptr<rom_data_block>> rom_data_blocks_;
        std::map<std::u16string, address> rom_data_addresses_;
        std::mutex rom_data_mutex_;
        address extension_rom_root_{};

    public:
        explicit memory_system(arm::exclusive_monitor *monitor, config::state *conf,
            const mem::mem_model_type model_type, const bool mem_map_old);

        ~memory_system();

        mem::control_base *get_control() {
            return impl_.get();
        }

        mem::mem_model_type get_model_type() const {
            return impl_->model_type();
        }

        // Immutable extracted ROM data survives file handles and all guest processes.
        address map_rom_data(const std::u16string &key, const std::vector<std::uint8_t> &bytes, address original_root = 0);
        address rom_root_directory(address original) const { return extension_rom_root_ ? extension_rom_root_ : original; }

        mem::mmu_base *get_mmu(arm::core *cc);
        const int get_page_size() const;

        void *get_real_pointer(const address addr, const mem::asid optional_asid = -1);

        bool read(const address addr, void *data, std::uint32_t size);
        bool write(const address addr, void *data, std::uint32_t size);

        template <typename T>
        T read(const address addr) {
            T data{};
            read(addr, &data, sizeof(T));

            return data;
        }

        template <typename T>
        bool write(const address addr, const T &val) {
            return write(addr, &val, sizeof(T));
        }
    };
}
