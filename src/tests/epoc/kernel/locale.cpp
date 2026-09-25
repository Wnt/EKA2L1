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

#include <catch2/catch.hpp>
#include <utils/locale.h>

#include <cstddef>
#include <cstring>

using namespace eka2l1;

// The first 0x88 bytes of the LOCALE.D00 the Nokia 9300 ROM (Symbian 7.0s) wrote after
// Clock > Change city > Helsinki (BaflUtils::PersistLocale); the rest of the 280 bytes is spare.
static const std::uint8_t HELSINKI_LOCALE_D00[] = {
    0x66, 0x01, 0x00, 0x00, 0x20, 0x1c, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2f, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3a, 0x00, 0x00, 0x00, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

TEST_CASE("the 7.0s LOCALE.Dnn file is a raw TLocale", "[locale]") {
    REQUIRE(offsetof(epoc::locale, spare_) == sizeof(HELSINKI_LOCALE_D00));

    epoc::locale loc{};
    std::memcpy(&loc, HELSINKI_LOCALE_D00, sizeof(HELSINKI_LOCALE_D00));

    REQUIRE(loc.country_code_ == 358);
    REQUIRE(loc.universal_time_offset_ == 7200);
    REQUIRE(loc.date_format_ == epoc::date_format_european);
    REQUIRE(loc.time_format_ == epoc::time_format_twenty_four_hours);
    REQUIRE(loc.date_separator_[1] == '/');
    REQUIRE(loc.time_separator_[1] == ':');
    REQUIRE(loc.home_daylight_saving_zone_ == epoc::daylight_saving_zone_european);
    REQUIRE(loc.work_days_ == 0x1F);
    REQUIRE(loc.digit_type_ == epoc::digit_type_western);

    // Saved in September: Helsinki on summer time, EEST = UTC+3.
    REQUIRE(epoc::locale_home_on_summer_time(loc));
    REQUIRE(epoc::locale_effective_utc_offset(loc) == 10800);
}

TEST_CASE("home summer time follows TLocale::QueryHomeHasDaylightSavingOn", "[locale]") {
    epoc::locale loc{};
    loc.universal_time_offset_ = -18000;
    loc.home_daylight_saving_zone_ = epoc::daylight_saving_zone_northern;

    loc.daylight_saving_ = epoc::daylight_saving_zone_european;
    REQUIRE_FALSE(epoc::locale_home_on_summer_time(loc));
    REQUIRE(epoc::locale_effective_utc_offset(loc) == -18000);

    loc.daylight_saving_ = epoc::daylight_saving_zone_northern | epoc::daylight_saving_zone_european;
    REQUIRE(epoc::locale_effective_utc_offset(loc) == -14400);

    // EDstHome counts as "home is on summer time" whatever the home zone is.
    loc.daylight_saving_ = epoc::daylight_saving_zone_dst_home;
    REQUIRE(epoc::locale_effective_utc_offset(loc) == -14400);
}
