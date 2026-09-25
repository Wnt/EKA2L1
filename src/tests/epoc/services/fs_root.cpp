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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include <services/fs/fs.h>

using namespace eka2l1;

// RFs::Entry/RFs::Att on a drive root fails with KErrBadName on the real file server. The Series 80 Messaging
// centre shows "Cannot find message storage" when the attribute query of "C:" succeeds.
TEST_CASE("fs_drive_root_path_is_recognised", "fs") {
    REQUIRE(is_drive_root_path(u"C:"));
    REQUIRE(is_drive_root_path(u"c:\\"));
    REQUIRE(is_drive_root_path(u"E:\\\\"));
    REQUIRE(is_drive_root_path(u"Z:/"));
}

TEST_CASE("fs_non_root_paths_keep_their_entries", "fs") {
    REQUIRE_FALSE(is_drive_root_path(u""));
    REQUIRE_FALSE(is_drive_root_path(u"C"));
    REQUIRE_FALSE(is_drive_root_path(u"C:\\System"));
    REQUIRE_FALSE(is_drive_root_path(u"C:\\System\\Mail\\"));
    REQUIRE_FALSE(is_drive_root_path(u"C:\\System\\Mail\\00001000"));
    REQUIRE_FALSE(is_drive_root_path(u"\\System"));
}
