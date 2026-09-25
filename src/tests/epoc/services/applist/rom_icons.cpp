// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>
#include <common/buffer.h>
#include <services/applist/applist.h>
#include <cstring>

TEST_CASE("ROM AIF v2 icon replies retain bitmap addresses instead of private FBS handles", "[rom-assets][applist]") {
    // One icon/mask pair embedded after the AIF's empty resource section.
    std::vector<std::uint8_t> data(180);
    auto put = [&](std::size_t offset, std::uint32_t value) { std::memcpy(data.data() + offset, &value, 4); };
    put(0, 0x101FB032);
    put(20, 0x10000041); put(24, 2); put(28, 16); put(32, 88);
    for (const std::size_t bitmap : {36, 108}) {
        put(bitmap, 0x10000040); put(bitmap + 4, 7); put(bitmap + 16, 4);
        put(bitmap + 20, 44); put(bitmap + 24, 40); put(bitmap + 28, 1); put(bitmap + 32, 1);
        put(bitmap + 44, 16); put(bitmap + 48, 1); put(bitmap + 64, 68);
    }
    eka2l1::common::ro_buf_stream stream(data.data(), data.size());
    std::vector<eka2l1::apa_app_icon> icons;
    // No host FBS is needed for native ROM objects.
    REQUIRE(eka2l1::read_icon_data_aif(&stream, nullptr, icons, 0x54000000, true));
    REQUIRE(icons.size() == 2);
    REQUIRE(icons[0].bmp_ == nullptr);
    REQUIRE(icons[1].bmp_ == nullptr);
    REQUIRE(icons[0].bmp_rom_addr_ == 0x54000024);
    REQUIRE(icons[1].bmp_rom_addr_ == 0x5400006c);
}
