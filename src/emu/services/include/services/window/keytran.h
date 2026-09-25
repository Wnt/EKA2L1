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

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eka2l1 {
    class kernel_system;
}

namespace eka2l1::epoc {
    /**
     * \brief Scan code to key code translation driven by the device's own keyboard data DLL.
     *
     * A real Symbian window server does not hardcode a keyboard. It loads a keyboard data library
     * (Z:\System\Libs\EKDATA.DLL, or the EKDATA.<nn>.DLL of the keyboard's language) and hands every
     * raw scan code to the key translator, which walks the tables that library exports:
     *
     * - a conversion table: modifier combinations to (scan code range -> key code) sub-tables,
     * - a modifier table: which keys turn Shift/Ctrl/Func/... on, off, or toggle them,
     * - per-state function tables: pass the key through, emit a special character,
     *   or collect Ctrl+digits into a character code,
     * - the list of key codes that must not auto-repeat, and the keypad scan codes.
     *
     * This class copies those tables out of the guest image once (the three exports of the data
     * library are run on a private interpreter core, so no layout or compiler assumption is made),
     * then keeps the same state the device keeps: the modifier word, the keyboard state and the
     * Ctrl+digits accumulator. It is a clean implementation of the documented table semantics.
     */
    class key_translator {
    public:
        struct masked_modifiers {
            std::uint32_t mask = 0;
            std::uint32_t value = 0;

            bool matches(const std::uint32_t modifiers) const {
                return (modifiers & mask) == value;
            }
        };

        struct scancode_block {
            std::uint16_t first = 0;
            std::uint16_t last = 0;
        };

        struct conv_sub_table {
            std::vector<scancode_block> blocks;
            std::vector<std::uint16_t> codes;
        };

        struct conv_node {
            masked_modifiers mods;
            std::vector<conv_sub_table> subs;
        };

        struct func_entry {
            masked_modifiers mods;
            std::uint16_t key_code = 0;
            std::uint8_t pattern = 0;
            std::uint8_t state = 0;
            std::uint8_t func = 0;
            std::int32_t param = 0;
        };

        using func_table = std::vector<func_entry>;

        struct result {
            bool produced = false; ///< An EEventKey should follow the key-down.
            std::uint32_t code = 0; ///< Key code for that event.
            std::uint32_t modifiers = 0; ///< Modifiers for that event.
        };

    private:
        std::vector<conv_node> conv_;
        std::uint32_t first_scancode_ = 0;
        std::uint32_t last_scancode_ = 0;
        std::vector<scancode_block> keypad_;
        std::vector<std::uint16_t> non_autorepeat_;
        func_table default_table_;
        func_table modifier_table_;
        std::vector<func_table> state_tables_;

        std::int32_t radix_ = 10;
        std::int32_t ctrl_digits_termination_ = 1;
        std::int32_t ctrl_digits_max_count_ = 3;
        std::int32_t ctrl_digits_limit_ = 10;

        std::uint32_t modifiers_ = 0;
        std::uint32_t state_;

        std::int32_t digit_count_ = 0;
        std::uint32_t digits_ = 0;
        bool digit_error_ = false;

        bool loaded_ = false;
        std::u16string source_;

        const func_entry *find_entry(const func_table &table, const std::uint32_t ch, const std::uint32_t mods) const;
        bool matches_pattern(const std::uint32_t ch, const func_entry &entry) const;
        bool currently_upper_case() const;
        std::uint32_t run_functions(const std::uint32_t ch);
        void reset_digits();
        void add_digit(const std::uint32_t ch);
        bool digits_terminated() const;

    public:
        explicit key_translator();

        /**
         * \brief Read the tables of a keyboard data library from the guest.
         *
         * \param kern     The kernel, used for the library manager and guest memory.
         * \param lib_name Library name, for example u"ekdata.dll".
         *
         * \returns True on success. On failure the translator stays unloaded.
         */
        bool load(kernel_system *kern, const std::u16string &lib_name);

        bool loaded() const {
            return loaded_;
        }

        const std::u16string &source() const {
            return source_;
        }

        /**
         * \brief Feed one raw key transition, exactly as the keyboard driver would.
         */
        result translate(const std::uint32_t scancode, const bool key_up);

        /**
         * \brief The persistent modifier state (Shift, Ctrl, Func, Caps Lock...) after the last key.
         */
        std::uint32_t modifier_state() const {
            return modifiers_;
        }

        /**
         * \brief Look up the conversion table for a scan code under the given modifiers.
         *
         * \param extra Receives the modifiers the table adds (autorepeatable, keypad, pure key code).
         */
        std::uint32_t convert(const std::uint32_t scancode, const std::uint32_t modifiers, std::uint32_t *extra = nullptr) const;

        bool is_autorepeatable(const std::uint32_t code) const;

        /**
         * \brief Find the key that types a character on this keyboard.
         *
         * The current modifier state is tried first, then the lock state alone, then Shift,
         * then Func (the Chr key on Series 80), then Shift+Func.
         *
         * \returns The scan code and the full modifier word to type the character with.
         */
        std::optional<std::pair<std::uint32_t, std::uint32_t>> find_key(const std::uint32_t code) const;

        /**
         * \brief The scan code of the key that has this character as its unshifted or shifted legend.
         */
        std::optional<std::uint32_t> find_scancode(const std::uint32_t code) const;
    };
}
