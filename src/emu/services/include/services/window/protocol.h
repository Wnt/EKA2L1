/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <services/window/common.h>
#include <services/window/op.h>
#include <utils/version.h>

namespace eka2l1::epoc {
    // The client build selects the wire ABI; OS 7.0 reports 151 with an earlier table.
    class window_server_protocol {
        epocver os_;
        version client_;

        bool versioned_opcodes() const {
            return client_.major == WS_MAJOR_VER && client_.minor == WS_MINOR_VER;
        }

        bool legacy_opcodes() const {
            return client_.build <= WS_OLDARCH_VER || os_ == epocver::epoc70;
        }

        // Series 80 v2 (Symbian OS 7.0s) ws32 reports 1.0.151 like S60v2, but its window table predates
        // AbsPosition. Derived from the push immediates of every export of the S80 DP2.0 SDK WS32.DLL:
        //   session opcodes    : identical to the modern table (StartCustomTextCursor 0x1c, SetSystemFaded 0x52)
        //   window opcodes     : 0x00-0x0b same; 0x0c (Size) .. 0x60 (SendPointerEvent) = modern - 1;
        //                        0x61 (GetDisplayMode) .. 0x70 (SetTransparencyBitmap) = modern - 2
        //   graphics context   : the build-139 table (BitBlt 0x30, DrawText 0x21, UseFont 0x37, Clear 0x40)
        //   screen device, sprite, anim DLL, click plug-in, DSA (old table): identical
        bool s80_opcodes() const {
            return os_ == epocver::epoc7 && client_.build == WS_NEWARCH_VER;
        }

    public:
        window_server_protocol(epocver os, version client) : os_(os), client_(client) {}

        std::uint16_t session_opcode(std::uint16_t opcode) const {
            if (versioned_opcodes() && legacy_opcodes()) {
                if (opcode >= ws_cl_op_start_custom_text_cursor) {
                    opcode += 2;
                }
                if (os_ == epocver::epoc70 && opcode >= ws_cl_op_set_faded) {
                    ++opcode;
                }
            }
            return opcode;
        }

        std::uint16_t window_opcode(std::uint16_t opcode) const {
            if (versioned_opcodes()) {
                if ((legacy_opcodes() || s80_opcodes()) && opcode >= EWsWinOpAbsPosition) {
                    ++opcode;
                }
                if (client_.build <= WS_NEWARCH_VER && os_ <= epocver::epoc94
                    && opcode >= EWsWinOpSendAdvancedPointerEvent) {
                    ++opcode;
                }
                if (os_ == epocver::epoc70 && opcode >= EWsWinOpEnableGroupListChangeEvents) {
                    opcode += 2;
                }
            }
            return opcode;
        }

        bool legacy_dsa() const {
            return client_.build <= WS_OLDARCH_VER || os_ <= epocver::epoc80;
        }

        // A sync-thread client of this table reads GetRegion's result as a rect count and expects 0.
        bool legacy_dsa_region() const {
            return legacy_opcodes();
        }
    };
}
