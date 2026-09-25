/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
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

#include <common/algorithm.h>
#include <common/fileutils.h>
#include <common/log.h>
#include <common/virtualmem.h>

#include <cpu/arm_interface.h>

#include <mem/allocator/std_page_allocator.h>
#include <mem/mem.h>
#include <mem/mmu.h>
#include <mem/ptr.h>

#include <algorithm>

namespace eka2l1 {
    memory_system::memory_system(arm::exclusive_monitor *monitor, config::state *conf,
        const mem::mem_model_type model_type, const bool mem_map_old)
        : conf_(conf) {
        alloc_ = std::make_unique<mem::basic_page_table_allocator>();
        impl_ = mem::make_new_control(monitor, alloc_.get(), conf_, 12, mem_map_old, model_type);
    }

    memory_system::~memory_system() {
        if (rom_map_) {
            common::unmap_file(rom_map_);
        }

        rom_data_blocks_.clear();
        impl_.reset();
        alloc_.reset();
    }

    address memory_system::map_rom_data(const std::u16string &key, const std::vector<std::uint8_t> &bytes, address original_root) {
        std::lock_guard<std::mutex> guard(rom_data_mutex_);
        const auto found = rom_data_addresses_.find(key);
        if (found != rom_data_addresses_.end()) {
            return found->second;
        }
        if (bytes.empty() || bytes.size() > 0x2000000 - 0x1000) {
            return 0;
        }
        const std::size_t aligned_size = (bytes.size() + 3) & ~std::size_t(3);
        rom_data_block *block = rom_data_blocks_.empty() ? nullptr : rom_data_blocks_.back().get();
        if (!block || aligned_size > block->bytes.size() - block->used) {
            auto fresh = std::make_unique<rom_data_block>();
            // EKA1 User::IsRomAddress recognizes one extension ROM through the
            // root-directory executive: its megabyte-aligned header has base/size
            // at +0x0c/+0x10. Reserve one contiguous, globally read-only region.
            // Refuse exhaustion rather than publishing an unrecognizable second ROM.
            if (block) return 0;
            const std::size_t size = 0x2100000;
            fresh->bytes.resize(size);
            fresh->chunk = mem::make_new_mem_model_chunk(impl_.get(), 0, get_model_type());
            mem::mem_model_chunk_creation_info info{};
            info.size = size;
            info.flags = mem::MEM_MODEL_CHUNK_REGION_USER_ROM | mem::MEM_MODEL_CHUNK_TYPE_NORMAL;
            info.perm = prot_read;
            info.host_map = fresh->bytes.data();
            if (!fresh->chunk || fresh->chunk->do_create(info) != mem::MEM_MODEL_CHUNK_ERR_OK) {
                return 0;
            }
            fresh->chunk->commit(0, size);
            if (fresh->chunk->committed() != size) return 0;
            const address chunk_base = fresh->chunk->base(nullptr);
            const address base = (chunk_base + 0xFFFFF) & ~address(0xFFFFF);
            const std::size_t prefix = base - chunk_base;
            const std::uint32_t rom_size = static_cast<std::uint32_t>(size - prefix);
            std::memcpy(fresh->bytes.data() + prefix + 0x0c, &base, 4);
            std::memcpy(fresh->bytes.data() + prefix + 0x10, &rom_size, 4);
            if (original_root) {
                auto *root = static_cast<std::uint32_t *>(get_real_pointer(original_root));
                if (!root || *root > (0x1000 - 0x24) / 8) return 0;
                // Preserve the complete root-directory list (variant/address pairs).
                std::memcpy(fresh->bytes.data() + prefix + 0x20, root, 4 + *root * 8);
                extension_rom_root_ = base + 0x20;
            }
            fresh->used = prefix + 0x1000;
            block = fresh.get();
            rom_data_blocks_.push_back(std::move(fresh));
        }
        const address result = block->chunk->base(nullptr) + static_cast<address>(block->used);
        std::memcpy(block->bytes.data() + block->used, bytes.data(), bytes.size());
        block->used += aligned_size;
        rom_data_addresses_.emplace(key, result);
        return result;
    }

    mem::mmu_base *memory_system::get_mmu(arm::core *cc) {
        return impl_->get_or_create_mmu(cc);
    }

    void *memory_system::get_real_pointer(const address addr, const mem::asid optional_asid) {
        if (addr == 0) {
            return nullptr;
        }

        return impl_->get_host_pointer(optional_asid, addr);
    }

    bool memory_system::read(const address addr, void *data, uint32_t size) {
        void *ptr = get_real_pointer(addr);

        if (!ptr) {
            return false;
        }

        std::memcpy(data, ptr, size);
        return true;
    }

    bool memory_system::write(const address addr, void *data, uint32_t size) {
        void *ptr = get_real_pointer(addr);

        if (!ptr) {
            return false;
        }

        std::memcpy(ptr, data, size);
        return true;
    }

    const int memory_system::get_page_size() const {
        return static_cast<int>(impl_->page_size());
    }
}
