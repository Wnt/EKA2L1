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

#include <services/msv/common.h>

#include <cstring>

using namespace eka2l1;

// The Series 80 message centre follows TMsvEntry::iRelatedId of the entry it gets: every field of the image the
// client receives must come from the entry, never from whatever the buffer held before.
TEST_CASE("msv_entry_data_carries_every_field", "msv") {
    epoc::msv::entry ent;
    ent.id_ = 0x100001;
    ent.parent_id_ = 0x1004;
    ent.data_ = 0x201;
    ent.pc_sync_count_ = 3;
    ent.reserved_ = 4;
    ent.service_id_ = 0x100000;
    ent.related_id_ = 0x1002;
    ent.type_uid_ = 0x10000F6A;
    ent.mtm_uid_ = 0x1000102C;
    ent.time_ = 0x00E0C6F121267DABULL;
    ent.size_ = 55;
    ent.error_ = -33;
    ent.bio_type_ = 7;
    ent.mtm_datas_[0] = 1;
    ent.mtm_datas_[1] = 2;
    ent.mtm_datas_[2] = 3;
    ent.description_ = u"Hello";
    ent.details_ = u"0401234567";

    epoc::msv::entry_data data;
    std::memset(&data, 0xAB, sizeof(data));
    epoc::msv::fill_entry_data(ent, data);

    REQUIRE(data.id_ == 0x100001);
    REQUIRE(data.parent_id_ == 0x1004);
    REQUIRE(data.data_ == 0x201);
    REQUIRE(data.pc_sync_count_ == 3);
    REQUIRE(data.reserved_ == 4);
    REQUIRE(data.service_id_ == 0x100000);
    REQUIRE(data.related_id_ == 0x1002);
    REQUIRE(data.type_uid_ == 0x10000F6A);
    REQUIRE(data.mtm_uid_ == 0x1000102C);
    REQUIRE(data.date_ == 0x00E0C6F121267DABULL);
    REQUIRE(data.size_ == 55);
    REQUIRE(data.error_ == -33);
    REQUIRE(data.bio_type_ == 7);
    REQUIRE(data.mtm_datas_[0] == 1);
    REQUIRE(data.mtm_datas_[2] == 3);
    REQUIRE(data.description_.get_length() == 5);
    REQUIRE(data.details_.get_length() == 10);

    epoc::msv::entry back;
    epoc::msv::apply_entry_data(data, back);
    REQUIRE(back.related_id_ == 0x1002);
    REQUIRE(back.error_ == -33);
    REQUIRE(back.mtm_datas_[1] == 2);
    REQUIRE(back.service_id_ == 0x100000);
}

TEST_CASE("msv_entry_data_of_a_fresh_entry_is_all_zero", "msv") {
    const epoc::msv::entry ent;

    epoc::msv::entry_data data;
    std::memset(&data, 0xAB, sizeof(data));
    epoc::msv::fill_entry_data(ent, data);

    REQUIRE(data.related_id_ == 0);
    REQUIRE(data.pc_sync_count_ == 0);
    REQUIRE(data.reserved_ == 0);
    REQUIRE(data.error_ == 0);
    REQUIRE(data.mtm_datas_[0] == 0);
    REQUIRE(data.mtm_datas_[1] == 0);
    REQUIRE(data.mtm_datas_[2] == 0);
}
