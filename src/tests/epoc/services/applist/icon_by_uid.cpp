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
#include <services/applist/applist.h>

using namespace eka2l1;

// Symbian 7.0s op 16 (GetAppIcon(TUid, TInt, ...)): the Series 80 file dialogs ask for icon 0 of each app.
TEST_CASE("applist_icon_by_uid_index_request", "applist") {
    int side_calls = 0;
    auto by_side = [&](const std::int32_t) -> std::size_t { side_calls++; return 7; };

    REQUIRE(applist_icon_pair_for_int_request(3, 0, by_side) == 0);
    REQUIRE(applist_icon_pair_for_int_request(3, 2, by_side) == 2);
    REQUIRE(side_calls == 0);
}

TEST_CASE("applist_icon_by_uid_side_request", "applist") {
    std::int32_t asked = 0;
    auto by_side = [&](const std::int32_t side) -> std::size_t { asked = side; return 1; };

    REQUIRE(applist_icon_pair_for_int_request(3, 25, by_side) == 1);
    REQUIRE(asked == 25);
    REQUIRE(applist_icon_pair_for_int_request(2, -1, by_side) == 1);
    REQUIRE(asked == -1);
}
