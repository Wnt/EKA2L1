// Private SysState / emulator bridge. Fixed-width EKA1 wire ABI, no SDK types.
#pragma once
namespace eka2l1_rom_bridge {
    enum { version = 1, max_groups = 64, name_capacity = 256 };
    struct group {
        int id;
        unsigned int thread;
        unsigned int name_length;
        unsigned short name[name_capacity];
    };
    struct snapshot {
        unsigned int protocol_version;
        unsigned int count;
        int focus;
        int switch_result;
        group groups[max_groups];
    };
}
