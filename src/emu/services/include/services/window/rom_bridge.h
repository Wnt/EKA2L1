#pragma once
#include <services/framework.h>
#include <services/window/rom_bridge_protocol.h>
#include <cstring>
#include <deque>
#include <vector>

namespace eka2l1 {
    // Only complete snapshots are published; a failed/oversized guest query preserves
    // the previous one. Consumers still validate that each owning thread is alive.
    class rom_window_snapshot {
    public:
        std::vector<eka2l1_rom_bridge::group> groups;
        int focus = 0;
        bool ready = false;
        bool assign(const void *bytes, std::size_t size) {
            using namespace eka2l1_rom_bridge;
            if (!bytes || size < 16) return false;
            unsigned int header[4];
            std::memcpy(header, bytes, sizeof(header));
            if (header[0] != version || header[1] > max_groups
                || size != 16 + header[1] * sizeof(group)) return false;
            std::vector<group> next(header[1]);
            if (!next.empty()) std::memcpy(next.data(), static_cast<const char *>(bytes) + 16, next.size() * sizeof(group));
            for (std::size_t i = 0; i < next.size(); ++i) {
                if (next[i].id <= 0 || next[i].name_length > name_capacity) return false;
                for (std::size_t j = 0; j < i; ++j) if (next[i].id == next[j].id) return false;
            }
            groups = std::move(next);
            focus = static_cast<int>(header[2]);
            ready = true;
            return true;
        }
    };

    class rom_window_bridge : public service::typical_server {
    public:
        rom_window_snapshot snapshot;
        std::deque<int> switches;
        explicit rom_window_bridge(eka2l1::system *sys);
        ~rom_window_bridge() override { clear_all_sessions(); }
        void connect(service::ipc_context &ctx) override;
    };
    rom_window_bridge *get_rom_window_bridge(kernel_system *kern);
}
