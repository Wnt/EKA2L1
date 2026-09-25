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

#include <services/window/keytran.h>

#include <common/cvt.h>
#include <common/log.h>
#include <common/types.h>

#include <cpu/arm_factory.h>
#include <kernel/codeseg.h>
#include <kernel/kernel.h>
#include <kernel/libmanager.h>
#include <mem/mem.h>

#include <algorithm>
#include <cstring>
#include <tuple>

namespace eka2l1::epoc {
    namespace {
        // TEventModifier (e32keys.h)
        constexpr std::uint32_t MOD_AUTOREPEATABLE = 0x00000001;
        constexpr std::uint32_t MOD_KEYPAD = 0x00000002;
        constexpr std::uint32_t MOD_LEFT_ALT = 0x00000004;
        constexpr std::uint32_t MOD_RIGHT_ALT = 0x00000008;
        constexpr std::uint32_t MOD_ALT = 0x00000010;
        constexpr std::uint32_t MOD_LEFT_CTRL = 0x00000020;
        constexpr std::uint32_t MOD_RIGHT_CTRL = 0x00000040;
        constexpr std::uint32_t MOD_CTRL = 0x00000080;
        constexpr std::uint32_t MOD_LEFT_SHIFT = 0x00000100;
        constexpr std::uint32_t MOD_RIGHT_SHIFT = 0x00000200;
        constexpr std::uint32_t MOD_SHIFT = 0x00000400;
        constexpr std::uint32_t MOD_LEFT_FUNC = 0x00000800;
        constexpr std::uint32_t MOD_RIGHT_FUNC = 0x00001000;
        constexpr std::uint32_t MOD_FUNC = 0x00002000;
        constexpr std::uint32_t MOD_CAPS_LOCK = 0x00004000;
        constexpr std::uint32_t MOD_NUM_LOCK = 0x00008000;
        constexpr std::uint32_t MOD_SCROLL_LOCK = 0x00010000;
        constexpr std::uint32_t MOD_KEY_UP = 0x00020000;
        constexpr std::uint32_t MOD_SPECIAL = 0x00040000;
        constexpr std::uint32_t MOD_PURE_KEYCODE = 0x00100000;
        constexpr std::uint32_t MOD_STICKY_OR_EXTEND = 0x00200000;

        // Modifiers the conversion table itself decides for a key.
        constexpr std::uint32_t MODS_FROM_TABLE = MOD_AUTOREPEATABLE | MOD_KEYPAD | MOD_PURE_KEYCODE;

        // Modifiers that outlive a key press.
        constexpr std::uint32_t MODS_PERSISTENT = MOD_LEFT_ALT | MOD_RIGHT_ALT | MOD_ALT | MOD_LEFT_CTRL | MOD_RIGHT_CTRL
            | MOD_CTRL | MOD_LEFT_SHIFT | MOD_RIGHT_SHIFT | MOD_SHIFT | MOD_LEFT_FUNC | MOD_RIGHT_FUNC | MOD_FUNC
            | MOD_CAPS_LOCK | MOD_NUM_LOCK | MOD_SCROLL_LOCK | MOD_STICKY_OR_EXTEND;

        // Keyboard states (k32keys.h TState)
        constexpr std::uint32_t STATE_NORMAL = 0x0A;
        constexpr std::uint32_t STATE_DIGITS_UNTIL_COUNT = 0x0B;
        constexpr std::uint32_t STATE_DIGITS_UNTIL_CTRL_UP = 0x0C;
        constexpr std::uint32_t STATE_UNCHANGED = 0x40;
        constexpr std::uint32_t STATE_FROM_DIGIT = 0x41;
        constexpr std::uint32_t STATE_CTRL_DIGITS = 0x42;

        // Functions of the state tables (TFuncGeneral) and of the modifier table (TModifierState)
        constexpr std::uint8_t FUNC_NOTHING = 0x00;
        constexpr std::uint8_t FUNC_PASS_KEY = 0x01;
        constexpr std::uint8_t FUNC_PASS_SPECIAL = 0x02;
        constexpr std::uint8_t FUNC_PASS_CTRL_DIGITS = 0x03;
        constexpr std::uint8_t FUNC_ADD_CTRL_DIGIT = 0x04;
        constexpr std::uint8_t FUNC_MODIFIER_ON = 0x40;
        constexpr std::uint8_t FUNC_MODIFIER_OFF = 0x41;
        constexpr std::uint8_t FUNC_MODIFIER_TOGGLE = 0x42;

        // Key code patterns (TPattern)
        constexpr std::uint8_t PATTERN_ANY_KEY = 0x00;
        constexpr std::uint8_t PATTERN_ANY_ALPHANUMERIC = 0x01;
        constexpr std::uint8_t PATTERN_ANY_ALPHA = 0x02;
        constexpr std::uint8_t PATTERN_ANY_LOWER = 0x03;
        constexpr std::uint8_t PATTERN_ANY_UPPER = 0x04;
        constexpr std::uint8_t PATTERN_ANY_DECIMAL = 0x05;
        constexpr std::uint8_t PATTERN_ANY_DIGIT_IN_RADIX = 0x06;
        constexpr std::uint8_t PATTERN_ANY_MODIFIER_KEY = 0x07;
        constexpr std::uint8_t PATTERN_MATCH_KEY = 0x40;
        constexpr std::uint8_t PATTERN_MATCH_KEY_NO_CASE = 0x41;
        constexpr std::uint8_t PATTERN_MATCH_LEFT_OR_RIGHT = 0x42;

        // TRadix
        constexpr std::int32_t RADIX_BINARY = 2;
        constexpr std::int32_t RADIX_OCTAL = 8;
        constexpr std::int32_t RADIX_HEX = 16;

        // TCtrlDigitsTermination
        constexpr std::int32_t TERMINATE_BY_COUNT = 0;
        constexpr std::int32_t TERMINATE_BY_CTRL_UP = 1;

        // Scan codes at or above this base (up to +0x100) are passed through untranslated.
        constexpr std::uint32_t SPECIAL_KEY_BASE = 0xF700;
        constexpr std::uint32_t SPECIAL_KEY_COUNT = 0x100;

        // EKeyLeftShift .. EKeyScrollLock
        constexpr std::uint32_t KEY_FIRST_MODIFIER = 0xF80B;
        constexpr std::uint32_t KEY_LAST_MODIFIER = 0xF815;

        constexpr std::uint32_t KEYBOARD_DATA_UID = 0x100039E0;

        constexpr std::uint32_t KEY_SPACE = 0x20;

        bool is_modifier_code(const std::uint32_t code) {
            return (code >= KEY_FIRST_MODIFIER) && (code <= KEY_LAST_MODIFIER);
        }

        // Character classes for the code points a keyboard table can produce (ASCII, Latin-1 and
        // Latin Extended-A). Private-use key codes (0xF7xx, 0xF8xx) are none of these.
        bool is_upper(const std::uint32_t c) {
            if ((c >= 'A') && (c <= 'Z'))
                return true;
            if ((c >= 0xC0) && (c <= 0xDE) && (c != 0xD7))
                return true;
            if ((c >= 0x100) && (c <= 0x17F))
                return (c != 0x138) && (c != 0x149) && (((c < 0x139) || (c > 0x148)) ? ((c & 1) == 0) : ((c & 1) == 1));
            return false;
        }

        bool is_lower(const std::uint32_t c) {
            if ((c >= 'a') && (c <= 'z'))
                return true;
            if ((c >= 0xDF) && (c <= 0xFF) && (c != 0xF7))
                return true;
            if ((c >= 0x100) && (c <= 0x17F))
                return !is_upper(c);
            return false;
        }

        bool is_alpha(const std::uint32_t c) {
            return is_upper(c) || is_lower(c) || (c == 0xAA) || (c == 0xBA);
        }

        bool is_decimal_digit(const std::uint32_t c) {
            return (c >= '0') && (c <= '9');
        }

        bool is_hex_digit(const std::uint32_t c) {
            return is_decimal_digit(c) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
        }

        std::uint32_t to_upper(const std::uint32_t c) {
            if ((c >= 'a') && (c <= 'z'))
                return c - 0x20;
            if ((c >= 0xE0) && (c <= 0xFE) && (c != 0xF7))
                return c - 0x20;
            if (c == 0xFF)
                return 0x178;
            if ((c >= 0x100) && (c <= 0x17F) && is_lower(c))
                return c - 1;
            return c;
        }

        std::uint32_t to_lower(const std::uint32_t c) {
            if ((c >= 'A') && (c <= 'Z'))
                return c + 0x20;
            if ((c >= 0xC0) && (c <= 0xDE) && (c != 0xD7))
                return c + 0x20;
            if (c == 0x178)
                return 0xFF;
            if ((c >= 0x100) && (c <= 0x17F) && is_upper(c))
                return c + 1;
            return c;
        }

        std::int32_t digit_value(const std::uint32_t c) {
            if (is_decimal_digit(c))
                return static_cast<std::int32_t>(c - '0');
            if ((c >= 'A') && (c <= 'F'))
                return static_cast<std::int32_t>(c - 'A' + 10);
            if ((c >= 'a') && (c <= 'f'))
                return static_cast<std::int32_t>(c - 'a' + 10);
            return -1;
        }

        bool is_digit_in_radix(const std::uint32_t c, const std::int32_t radix) {
            switch (radix) {
            case RADIX_BINARY:
                return (c == '0') || (c == '1');
            case RADIX_OCTAL:
                return is_decimal_digit(c) && (c != '8') && (c != '9');
            case RADIX_HEX:
                return is_hex_digit(c);
            default:
                break;
            }
            return is_decimal_digit(c);
        }

        // A tiny, private guest call environment: an interpreter core whose memory is the guest image
        // (read-only, through the memory system) plus a host scratch page for stack and outputs.
        class guest_caller {
            static constexpr std::uint32_t SCRATCH_BASE = 0x00010000;
            static constexpr std::uint32_t SCRATCH_SIZE = 0x4000;
            static constexpr std::uint32_t RETURN_ADDRESS = SCRATCH_BASE;
            static constexpr std::uint32_t OUTPUT_BASE = SCRATCH_BASE + 0x100;
            static constexpr std::uint32_t OUTPUT_STRIDE = 0x40;
            static constexpr std::uint32_t STACK_TOP = SCRATCH_BASE + SCRATCH_SIZE - 0x100;
            static constexpr int MAX_STEPS = 20000;

            memory_system *mem_;
            arm::exclusive_monitor_instance monitor_;
            arm::core_instance core_;
            std::vector<std::uint8_t> scratch_;
            bool faulted_ = false;

            bool in_scratch(const std::uint32_t addr, const std::uint32_t size) const {
                return (addr >= SCRATCH_BASE) && (addr + size <= SCRATCH_BASE + SCRATCH_SIZE) && (addr + size > addr);
            }

            bool read(const std::uint32_t addr, void *dest, const std::uint32_t size) {
                if (in_scratch(addr, size)) {
                    std::memcpy(dest, scratch_.data() + (addr - SCRATCH_BASE), size);
                    return true;
                }

                return mem_->read(addr, dest, size);
            }

            bool write(const std::uint32_t addr, const void *src, const std::uint32_t size) {
                if (!in_scratch(addr, size)) {
                    return false;
                }

                std::memcpy(scratch_.data() + (addr - SCRATCH_BASE), src, size);
                return true;
            }

        public:
            explicit guest_caller(memory_system *mem)
                : mem_(mem)
                , scratch_(SCRATCH_SIZE, 0) {
                monitor_ = arm::create_exclusive_monitor(arm_emulator_type::dyncom, 1);
                core_ = arm::create_core(monitor_.get(), arm_emulator_type::dyncom);

                if (!core_) {
                    return;
                }

                core_->read_8bit = [this](arm::address a, std::uint8_t *d) { return read(a, d, 1); };
                core_->read_16bit = [this](arm::address a, std::uint16_t *d) { return read(a, d, 2); };
                core_->read_32bit = [this](arm::address a, std::uint32_t *d) { return read(a, d, 4); };
                core_->read_64bit = [this](arm::address a, std::uint64_t *d) { return read(a, d, 8); };
                core_->read_code = [this](arm::address a, std::uint32_t *d) { return read(a, d, 4); };
                core_->write_8bit = [this](arm::address a, std::uint8_t *d) { return write(a, d, 1); };
                core_->write_16bit = [this](arm::address a, std::uint16_t *d) { return write(a, d, 2); };
                core_->write_32bit = [this](arm::address a, std::uint32_t *d) { return write(a, d, 4); };
                core_->write_64bit = [this](arm::address a, std::uint64_t *d) { return write(a, d, 8); };
                core_->exclusive_write_8bit = [this](arm::address a, std::uint8_t v, std::uint8_t) { return write(a, &v, 1) ? 1 : -1; };
                core_->exclusive_write_16bit = [this](arm::address a, std::uint16_t v, std::uint16_t) { return write(a, &v, 2) ? 1 : -1; };
                core_->exclusive_write_32bit = [this](arm::address a, std::uint32_t v, std::uint32_t) { return write(a, &v, 4) ? 1 : -1; };
                core_->exclusive_write_64bit = [this](arm::address a, std::uint64_t v, std::uint64_t) { return write(a, &v, 8) ? 1 : -1; };
                core_->system_call_handler = [this](const std::uint32_t) {
                    faulted_ = true;
                    core_->stop();
                };
                core_->exception_handler = [this](arm::exception_type, const std::uint32_t) {
                    faulted_ = true;
                    core_->stop();
                    return false;
                };
            }

            bool valid() const {
                return core_ != nullptr;
            }

            static std::uint32_t output(const int index) {
                return OUTPUT_BASE + static_cast<std::uint32_t>(index) * OUTPUT_STRIDE;
            }

            // Call a guest leaf function with pointer arguments; the first four go in r0-r3, the rest on the stack.
            bool call(const std::uint32_t function, const std::vector<std::uint32_t> &args) {
                std::fill(scratch_.begin(), scratch_.end(), 0);
                faulted_ = false;

                std::uint32_t sp = STACK_TOP;
                for (std::size_t i = 4; i < args.size(); i++) {
                    write(sp + static_cast<std::uint32_t>(i - 4) * 4, &args[i], 4);
                }

                for (std::size_t i = 0; i < 16; i++) {
                    core_->set_reg(i, (i < args.size() && i < 4) ? args[i] : 0);
                }

                core_->set_sp(sp);
                core_->set_lr(RETURN_ADDRESS);
                core_->set_cpsr((function & 1) ? 0x30 : 0x10);
                core_->set_pc(function & ~1u);
                core_->clear_instruction_cache();

                for (int step = 0; step < MAX_STEPS; step++) {
                    core_->step();

                    if (faulted_) {
                        return false;
                    }

                    if ((core_->get_pc() & ~1u) == RETURN_ADDRESS) {
                        return true;
                    }
                }

                return false;
            }

            std::uint32_t read_output_word(const int index, const int word) {
                std::uint32_t value = 0;
                read(output(index) + static_cast<std::uint32_t>(word) * 4, &value, 4);
                return value;
            }
        };

        struct guest_reader {
            memory_system *mem;
            bool ok = true;

            std::uint32_t u32(const std::uint32_t addr) {
                std::uint32_t v = 0;
                if (!mem->read(addr, &v, 4))
                    ok = false;
                return v;
            }

            std::uint16_t u16(const std::uint32_t addr) {
                std::uint16_t v = 0;
                if (!mem->read(addr, &v, 2))
                    ok = false;
                return v;
            }

            std::uint8_t u8(const std::uint32_t addr) {
                std::uint8_t v = 0;
                if (!mem->read(addr, &v, 1))
                    ok = false;
                return v;
            }
        };

        // Sanity bounds for tables read from guest memory.
        constexpr std::uint32_t MAX_TABLE_ITEMS = 1024;
    }

    key_translator::key_translator()
        : state_(STATE_NORMAL) {
    }

    bool key_translator::load(kernel_system *kern, const std::u16string &lib_name) {
        loaded_ = false;

        hle::lib_manager *libmngr = kern->get_lib_manager();
        codeseg_ptr seg = libmngr ? libmngr->load(lib_name) : nullptr;

        if (!seg) {
            LOG_WARN(SERVICE_WINDOW, "Key translator: keyboard data library {} not found", common::ucs2_to_utf8(lib_name));
            return false;
        }

        const std::uint32_t uid3 = std::get<2>(seg->get_uids());

        if (uid3 != KEYBOARD_DATA_UID) {
            LOG_WARN(SERVICE_WINDOW, "Key translator: {} is not a keyboard data library (UID3 0x{:X})",
                common::ucs2_to_utf8(lib_name), uid3);
            return false;
        }

        const address conv_fn = seg->lookup_no_relocate(1);
        const address func_fn = seg->lookup_no_relocate(2);
        const address settings_fn = seg->lookup_no_relocate(3);

        if (!conv_fn || !func_fn || !settings_fn) {
            LOG_WARN(SERVICE_WINDOW, "Key translator: {} lacks the three keyboard data exports", common::ucs2_to_utf8(lib_name));
            return false;
        }

        memory_system *mem = kern->get_memory_system();
        guest_caller caller(mem);

        if (!caller.valid()) {
            LOG_ERROR(SERVICE_WINDOW, "Key translator: no interpreter core available");
            return false;
        }

        // KeyDataConvTable(SConvTable&, TUint& first, TUint& last, SScanCodeBlockList& keypad, SKeyCodeList& nonAutorepeat)
        if (!caller.call(conv_fn, { guest_caller::output(0), guest_caller::output(1), guest_caller::output(2),
                guest_caller::output(3), guest_caller::output(4) })) {
            LOG_ERROR(SERVICE_WINDOW, "Key translator: running KeyDataConvTable of {} failed", common::ucs2_to_utf8(lib_name));
            return false;
        }

        const std::uint32_t conv_count = caller.read_output_word(0, 0);
        const std::uint32_t conv_nodes = caller.read_output_word(0, 1);
        const std::uint32_t first_scancode = caller.read_output_word(1, 0);
        const std::uint32_t last_scancode = caller.read_output_word(2, 0);
        const std::uint32_t keypad_count = caller.read_output_word(3, 0);
        const std::uint32_t keypad_blocks = caller.read_output_word(3, 1);
        const std::uint32_t nonauto_count = caller.read_output_word(4, 0);
        const std::uint32_t nonauto_codes = caller.read_output_word(4, 1);

        // KeyDataFuncTable(SFuncTables&)
        if (!caller.call(func_fn, { guest_caller::output(0) })) {
            LOG_ERROR(SERVICE_WINDOW, "Key translator: running KeyDataFuncTable of {} failed", common::ucs2_to_utf8(lib_name));
            return false;
        }

        const std::uint32_t default_count = caller.read_output_word(0, 0);
        const std::uint32_t default_entries = caller.read_output_word(0, 1);
        const std::uint32_t modifier_count = caller.read_output_word(0, 2);
        const std::uint32_t modifier_entries = caller.read_output_word(0, 3);
        const std::uint32_t state_count = caller.read_output_word(0, 4);
        const std::uint32_t state_tables = caller.read_output_word(0, 5);

        // KeyDataSettings(TRadix&, TCtrlDigitsTermination&, TInt& defaultMaxCount, TInt& maximumMaxCount)
        if (!caller.call(settings_fn, { guest_caller::output(0), guest_caller::output(1), guest_caller::output(2),
                guest_caller::output(3) })) {
            LOG_ERROR(SERVICE_WINDOW, "Key translator: running KeyDataSettings of {} failed", common::ucs2_to_utf8(lib_name));
            return false;
        }

        const std::int32_t radix = static_cast<std::int32_t>(caller.read_output_word(0, 0));
        const std::int32_t termination = static_cast<std::int32_t>(caller.read_output_word(1, 0));
        const std::int32_t max_count = static_cast<std::int32_t>(caller.read_output_word(2, 0));
        const std::int32_t limit = static_cast<std::int32_t>(caller.read_output_word(3, 0));

        if ((conv_count == 0) || (conv_count > MAX_TABLE_ITEMS) || (keypad_count > MAX_TABLE_ITEMS) || (nonauto_count > MAX_TABLE_ITEMS)
            || (default_count > MAX_TABLE_ITEMS) || (modifier_count > MAX_TABLE_ITEMS) || (state_count > MAX_TABLE_ITEMS)) {
            LOG_ERROR(SERVICE_WINDOW, "Key translator: implausible table sizes in {}", common::ucs2_to_utf8(lib_name));
            return false;
        }

        guest_reader rd{ mem };

        std::vector<conv_node> conv;
        for (std::uint32_t n = 0; n < conv_count; n++) {
            // SConvTableNode: { TMaskedModifiers (8), TUint numSubTables, const SConvSubTable* const* }
            const std::uint32_t node = conv_nodes + n * 16;
            conv_node cn;
            cn.mods.mask = rd.u32(node);
            cn.mods.value = rd.u32(node + 4);
            const std::uint32_t sub_count = rd.u32(node + 8);
            const std::uint32_t sub_ptrs = rd.u32(node + 12);

            if (sub_count > MAX_TABLE_ITEMS) {
                rd.ok = false;
                break;
            }

            for (std::uint32_t s = 0; s < sub_count; s++) {
                // SConvSubTable: { const TUint16* keyCodes, SScanCodeBlockList { TUint numBlocks, const SScanCodeBlock* } }
                const std::uint32_t sub = rd.u32(sub_ptrs + s * 4);
                const std::uint32_t codes = rd.u32(sub);
                const std::uint32_t block_count = rd.u32(sub + 4);
                const std::uint32_t blocks = rd.u32(sub + 8);

                if (block_count > MAX_TABLE_ITEMS) {
                    rd.ok = false;
                    break;
                }

                conv_sub_table cs;
                std::uint32_t total = 0;

                for (std::uint32_t b = 0; b < block_count; b++) {
                    scancode_block blk;
                    blk.first = rd.u16(blocks + b * 4);
                    blk.last = rd.u16(blocks + b * 4 + 2);

                    if (blk.last < blk.first) {
                        rd.ok = false;
                        break;
                    }

                    total += blk.last - blk.first + 1U;
                    cs.blocks.push_back(blk);
                }

                if (total > 0x10000) {
                    rd.ok = false;
                    break;
                }

                for (std::uint32_t k = 0; k < total; k++) {
                    cs.codes.push_back(rd.u16(codes + k * 2));
                }

                cn.subs.push_back(std::move(cs));
            }

            conv.push_back(std::move(cn));
        }

        std::vector<scancode_block> keypad;
        for (std::uint32_t b = 0; b < keypad_count; b++) {
            keypad.push_back({ rd.u16(keypad_blocks + b * 4), rd.u16(keypad_blocks + b * 4 + 2) });
        }

        std::vector<std::uint16_t> nonauto;
        for (std::uint32_t k = 0; k < nonauto_count; k++) {
            nonauto.push_back(rd.u16(nonauto_codes + k * 2));
        }

        // SFuncTableEntry: { TMaskedModifiers (8), TKeyCodePattern { TUint16 code, TInt8 pattern, TInt8 filler },
        //                    SFuncAndState { TUint8 state, TUint8 func, (pad 2), TInt32 param } } = 20 bytes
        auto read_func_table = [&](const std::uint32_t count, const std::uint32_t entries) {
            func_table table;
            if (count > MAX_TABLE_ITEMS) {
                rd.ok = false;
                return table;
            }
            for (std::uint32_t i = 0; i < count; i++) {
                const std::uint32_t e = entries + i * 20;
                func_entry fe;
                fe.mods.mask = rd.u32(e);
                fe.mods.value = rd.u32(e + 4);
                fe.key_code = rd.u16(e + 8);
                fe.pattern = rd.u8(e + 10);
                fe.state = rd.u8(e + 12);
                fe.func = rd.u8(e + 13);
                fe.param = static_cast<std::int32_t>(rd.u32(e + 16));
                table.push_back(fe);
            }
            return table;
        };

        func_table default_table = read_func_table(default_count, default_entries);
        func_table modifier_table = read_func_table(modifier_count, modifier_entries);

        std::vector<func_table> states;
        for (std::uint32_t s = 0; s < state_count; s++) {
            // SFuncTable: { TUint numEntries, const SFuncTableEntry* }
            const std::uint32_t count = rd.u32(state_tables + s * 8);
            const std::uint32_t entries = rd.u32(state_tables + s * 8 + 4);
            states.push_back(read_func_table(count, entries));
        }

        if (!rd.ok || default_table.empty()) {
            LOG_ERROR(SERVICE_WINDOW, "Key translator: tables of {} could not be read", common::ucs2_to_utf8(lib_name));
            return false;
        }

        conv_ = std::move(conv);
        first_scancode_ = first_scancode;
        last_scancode_ = last_scancode;
        keypad_ = std::move(keypad);
        non_autorepeat_ = std::move(nonauto);
        default_table_ = std::move(default_table);
        modifier_table_ = std::move(modifier_table);
        state_tables_ = std::move(states);
        radix_ = radix;
        ctrl_digits_termination_ = termination;
        ctrl_digits_limit_ = limit;
        ctrl_digits_max_count_ = std::min(max_count, limit);

        modifiers_ = 0;
        state_ = STATE_NORMAL;
        reset_digits();

        source_ = lib_name;
        loaded_ = true;

        LOG_INFO(SERVICE_WINDOW, "Key translator: {} loaded ({} conversion nodes, {} modifier rules, {} states, scan codes 0x{:X}-0x{:X})",
            common::ucs2_to_utf8(lib_name), conv_.size(), modifier_table_.size(), state_tables_.size(), first_scancode_, last_scancode_);

        return true;
    }

    std::uint32_t key_translator::convert(const std::uint32_t scancode, const std::uint32_t modifiers, std::uint32_t *extra) const {
        if (extra) {
            *extra = 0;
        }

        for (const conv_node &node : conv_) {
            if (!node.mods.matches(modifiers)) {
                continue;
            }

            for (const conv_sub_table &sub : node.subs) {
                std::uint32_t offset = 0;

                for (const scancode_block &blk : sub.blocks) {
                    if ((scancode >= blk.first) && (scancode <= blk.last)) {
                        const std::uint32_t index = offset + (scancode - blk.first);
                        const std::uint32_t code = (index < sub.codes.size()) ? sub.codes[index] : 0;

                        if (extra) {
                            std::uint32_t added = 0;

                            for (const scancode_block &kp : keypad_) {
                                if ((scancode >= kp.first) && (scancode <= kp.last)) {
                                    added |= MOD_KEYPAD;
                                    break;
                                }
                            }

                            if (is_autorepeatable(code)) {
                                added |= MOD_AUTOREPEATABLE;
                            }

                            // Ctrl is held but this node does not look at Ctrl: the key code is not a control code.
                            if ((modifiers & MOD_CTRL) && !(node.mods.mask & MOD_CTRL)) {
                                added |= MOD_PURE_KEYCODE;
                            }

                            *extra = added;
                        }

                        return code;
                    }

                    offset += blk.last - blk.first + 1U;
                }
            }
        }

        return 0;
    }

    bool key_translator::is_autorepeatable(const std::uint32_t code) const {
        for (const std::uint16_t c : non_autorepeat_) {
            if (c == code) {
                return false;
            }
        }

        return true;
    }

    bool key_translator::matches_pattern(const std::uint32_t ch, const func_entry &entry) const {
        switch (entry.pattern) {
        case PATTERN_ANY_KEY:
            return true;
        case PATTERN_ANY_ALPHANUMERIC:
            return is_alpha(ch) || is_decimal_digit(ch);
        case PATTERN_ANY_ALPHA:
            return is_alpha(ch);
        case PATTERN_ANY_LOWER:
            return is_lower(ch);
        case PATTERN_ANY_UPPER:
            return is_upper(ch);
        case PATTERN_ANY_DECIMAL:
            return is_decimal_digit(ch);
        case PATTERN_ANY_DIGIT_IN_RADIX:
            return is_digit_in_radix(ch, radix_);
        case PATTERN_ANY_MODIFIER_KEY:
            return is_modifier_code(ch);
        case PATTERN_MATCH_KEY:
            return ch == entry.key_code;
        case PATTERN_MATCH_KEY_NO_CASE:
            return to_lower(ch) == to_lower(entry.key_code);
        case PATTERN_MATCH_LEFT_OR_RIGHT:
            return (ch == entry.key_code) || (ch == entry.key_code + 1U);
        default:
            break;
        }

        return false;
    }

    const key_translator::func_entry *key_translator::find_entry(const func_table &table, const std::uint32_t ch, const std::uint32_t mods) const {
        for (const func_entry &e : table) {
            if (matches_pattern(ch, e) && e.mods.matches(mods)) {
                return &e;
            }
        }

        return nullptr;
    }

    bool key_translator::currently_upper_case() const {
        // Whether a letter typed now would come out upper case, judged by the table itself: the
        // first letter any scan code produces under the current Shift/Caps Lock state decides.
        const std::uint32_t mods = modifiers_ & (MOD_CAPS_LOCK | MOD_SHIFT);

        for (std::uint32_t sc = first_scancode_; sc <= last_scancode_; sc++) {
            const std::uint32_t c = convert(sc, mods);

            if (is_upper(c)) {
                return true;
            }

            if (is_lower(c)) {
                return false;
            }
        }

        return false;
    }

    void key_translator::reset_digits() {
        digit_count_ = 0;
        digits_ = 0;
        digit_error_ = false;
    }

    void key_translator::add_digit(const std::uint32_t ch) {
        digit_count_++;
        digits_ = digits_ * static_cast<std::uint32_t>(radix_) + static_cast<std::uint32_t>(std::max(digit_value(ch), 0));

        if (!is_digit_in_radix(ch, radix_) || (digit_value(ch) < 0)) {
            digit_error_ = true;
        }

        if ((ctrl_digits_termination_ == TERMINATE_BY_CTRL_UP) && !(modifiers_ & MOD_CTRL)) {
            digit_error_ = true;
        }
    }

    bool key_translator::digits_terminated() const {
        if ((ctrl_digits_termination_ == TERMINATE_BY_COUNT) && (digit_count_ >= ctrl_digits_max_count_)) {
            return true;
        }

        if ((ctrl_digits_termination_ == TERMINATE_BY_CTRL_UP) && !(modifiers_ & MOD_CTRL)) {
            return true;
        }

        return digit_count_ >= ctrl_digits_limit_;
    }

    std::uint32_t key_translator::run_functions(const std::uint32_t ch) {
        // What this key does to the modifiers: the first matching modifier rule, else the default rule.
        std::uint8_t modifier_func = FUNC_NOTHING;
        std::int32_t modifier_param = 0;

        const func_entry *def = find_entry(default_table_, ch, modifiers_);

        if (def) {
            modifier_func = def->func;
            modifier_param = def->param;
        }

        if (const func_entry *mod = find_entry(modifier_table_, ch, modifiers_)) {
            modifier_func = mod->func;
            modifier_param = mod->param;
        }

        // What it does to the text and the keyboard state: the current state's table, else the default rule.
        std::uint8_t func = FUNC_NOTHING;
        std::uint32_t new_state = STATE_UNCHANGED;
        std::int32_t param = 0;

        const func_entry *gen = (state_ < state_tables_.size()) ? find_entry(state_tables_[state_], ch, modifiers_) : nullptr;

        if (!gen) {
            gen = def;
        }

        if (gen) {
            func = gen->func;
            new_state = gen->state;
            param = gen->param;
        }

        switch (modifier_func) {
        case FUNC_MODIFIER_ON:
            modifiers_ |= static_cast<std::uint32_t>(modifier_param);
            break;
        case FUNC_MODIFIER_OFF:
            modifiers_ &= ~static_cast<std::uint32_t>(modifier_param);
            break;
        case FUNC_MODIFIER_TOGGLE:
            modifiers_ ^= static_cast<std::uint32_t>(modifier_param);
            break;
        default:
            break;
        }

        // A combined modifier (Shift, Ctrl, Func, Alt) stays on only while one of its halves is held.
        if (!(modifiers_ & (MOD_LEFT_ALT | MOD_RIGHT_ALT)))
            modifiers_ &= ~MOD_ALT;
        if (!(modifiers_ & (MOD_LEFT_SHIFT | MOD_RIGHT_SHIFT)))
            modifiers_ &= ~MOD_SHIFT;
        if (!(modifiers_ & (MOD_LEFT_FUNC | MOD_RIGHT_FUNC)))
            modifiers_ &= ~MOD_FUNC;
        if (!(modifiers_ & (MOD_LEFT_CTRL | MOD_RIGHT_CTRL)))
            modifiers_ &= ~MOD_CTRL;

        std::uint32_t code = 0;

        switch (func) {
        case FUNC_PASS_KEY:
            code = ch;
            break;

        case FUNC_PASS_SPECIAL:
            reset_digits();
            code = currently_upper_case() ? to_upper(static_cast<std::uint32_t>(param)) : static_cast<std::uint32_t>(param);
            modifiers_ |= MOD_SPECIAL;
            break;

        case FUNC_PASS_CTRL_DIGITS:
            if (digits_ <= 0xFFFF) {
                code = digits_;
                modifiers_ |= MOD_SPECIAL;
            }
            reset_digits();
            break;

        case FUNC_ADD_CTRL_DIGIT:
            add_digit(ch);
            if (digits_terminated() && !digit_error_ && (digits_ <= 0xFFFF)) {
                code = digits_;
                modifiers_ |= MOD_SPECIAL;
            }
            break;

        default:
            break;
        }

        switch (new_state) {
        case STATE_UNCHANGED:
            break;

        case STATE_FROM_DIGIT: {
            const std::int32_t value = digit_value(ch);
            if (value >= 0) {
                state_ = static_cast<std::uint32_t>(value);
            }
            break;
        }

        case STATE_CTRL_DIGITS:
            if (digits_terminated() || digit_error_) {
                state_ = STATE_NORMAL;
                reset_digits();
            } else {
                state_ = (ctrl_digits_termination_ == TERMINATE_BY_COUNT) ? STATE_DIGITS_UNTIL_COUNT : STATE_DIGITS_UNTIL_CTRL_UP;
            }
            break;

        default:
            state_ = new_state;
            if (state_ == STATE_NORMAL) {
                reset_digits();
            }
            break;
        }

        if (state_ >= state_tables_.size()) {
            state_ = STATE_NORMAL;
        }

        return code;
    }

    key_translator::result key_translator::translate(const std::uint32_t scancode, const bool key_up) {
        result res;

        if (!loaded_) {
            return res;
        }

        const std::uint32_t previous_state = state_;
        modifiers_ &= ~MOD_PURE_KEYCODE;

        std::uint32_t ch = scancode;

        if ((scancode < SPECIAL_KEY_BASE) || (scancode >= SPECIAL_KEY_BASE + SPECIAL_KEY_COUNT)) {
            // Outside the normal state only Num Lock may influence the conversion.
            const std::uint32_t conv_mods = (state_ == STATE_NORMAL) ? modifiers_ : (modifiers_ & MOD_NUM_LOCK);
            std::uint32_t table_mods = 0;

            ch = convert(scancode, conv_mods, &table_mods);
            modifiers_ = (modifiers_ & ~MODS_FROM_TABLE) | (table_mods & MODS_FROM_TABLE);
        }

        if (key_up) {
            modifiers_ |= MOD_KEY_UP;
        } else {
            modifiers_ &= ~MOD_KEY_UP;
        }

        std::uint32_t code = run_functions(ch);

        // Modifier keys never come out as key presses.
        if (is_modifier_code(code)) {
            code = 0;
            modifiers_ &= ~MOD_PURE_KEYCODE;
        }

        res.produced = (code != 0);

        if (key_up || (code == 0) || (state_ != STATE_NORMAL) || (state_ != previous_state)) {
            modifiers_ &= ~MOD_AUTOREPEATABLE;
        }

        // Ctrl+Space is reported, but with a null key code.
        if ((code == KEY_SPACE) && (modifiers_ & MOD_CTRL)) {
            code = 0;
            modifiers_ &= ~MOD_PURE_KEYCODE;
        }

        res.code = code;
        res.modifiers = modifiers_;

        modifiers_ &= MODS_PERSISTENT;
        return res;
    }

    std::optional<std::pair<std::uint32_t, std::uint32_t>> key_translator::find_key(const std::uint32_t code) const {
        if (!loaded_ || (code == 0)) {
            return std::nullopt;
        }

        const std::uint32_t locks = modifiers_ & (MOD_CAPS_LOCK | MOD_NUM_LOCK | MOD_SCROLL_LOCK);
        const std::uint32_t shift = MOD_LEFT_SHIFT | MOD_SHIFT;
        const std::uint32_t func = MOD_LEFT_FUNC | MOD_FUNC;
        const std::uint32_t current = modifiers_ & MODS_PERSISTENT;

        std::vector<std::uint32_t> candidates;

        // The state the user is in first, unless it has Ctrl in it (Ctrl turns letters into control codes).
        if (!(current & MOD_CTRL)) {
            candidates.push_back(current);
        }

        candidates.push_back(locks);
        candidates.push_back(locks | shift);
        candidates.push_back(locks | func);
        candidates.push_back(locks | shift | func);

        for (const std::uint32_t mods : candidates) {
            for (const conv_node &node : conv_) {
                if (!node.mods.matches(mods)) {
                    continue;
                }

                for (const conv_sub_table &sub : node.subs) {
                    std::uint32_t offset = 0;

                    for (const scancode_block &blk : sub.blocks) {
                        for (std::uint32_t sc = blk.first; sc <= blk.last; sc++) {
                            if ((offset + sc - blk.first < sub.codes.size()) && (sub.codes[offset + sc - blk.first] == code)) {
                                // Make sure this node is the one that wins for this scan code.
                                if (convert(sc, mods) == code) {
                                    return std::make_pair(sc, mods);
                                }
                            }
                        }

                        offset += blk.last - blk.first + 1U;
                    }
                }
            }
        }

        return std::nullopt;
    }

    std::optional<std::uint32_t> key_translator::find_scancode(const std::uint32_t code) const {
        if (!loaded_ || (code == 0)) {
            return std::nullopt;
        }

        const std::uint32_t candidates[] = { 0, MOD_LEFT_SHIFT | MOD_SHIFT, MOD_CAPS_LOCK, MOD_LEFT_FUNC | MOD_FUNC };

        for (const std::uint32_t mods : candidates) {
            for (std::uint32_t sc = first_scancode_; sc <= last_scancode_; sc++) {
                if (convert(sc, mods) == code) {
                    return sc;
                }
            }
        }

        return std::nullopt;
    }
}
