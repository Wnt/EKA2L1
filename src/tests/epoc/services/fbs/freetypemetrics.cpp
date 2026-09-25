// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>
#include <services/fbs/adapter/freetype_font_adapter.h>
#include <fstream>
#include <iterator>

TEST_CASE("FreeType design size preserves the requested em height", "fbs") {
    std::ifstream file("fontassets/overlay_font.ttf", std::ios::binary);
    REQUIRE(file.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    eka2l1::epoc::adapter::freetype_font_adapter adapter(bytes);
    REQUIRE(adapter.is_valid());
    FT_Library library = nullptr;
    REQUIRE(FT_Init_FreeType(&library) == 0);
    FT_Face face = nullptr;
    REQUIRE(FT_New_Memory_Face(library, bytes.data(), bytes.size(), 0, &face) == 0);
    for (const auto size : {10, 18, 20, 32}) {
        std::uint32_t id = 0;
        const auto metric = adapter.get_nearest_supported_metric(0, size, &id, true);
        REQUIRE(metric.has_value());
        REQUIRE(metric->design_height == size);
        REQUIRE(FT_Set_Pixel_Sizes(face, 0, size) == 0);
        REQUIRE(FT_Load_Char(face, 'M', FT_LOAD_DEFAULT) == 0);
        int width = 0, height = 0;
        std::uint32_t total = 0;
        eka2l1::epoc::glyph_bitmap_type type{};
        eka2l1::epoc::open_font_character_metric glyph{};
        auto bitmap = adapter.get_glyph_bitmap(0, 'M', id, &width, &height, total, &type, glyph);
        REQUIRE(bitmap != nullptr);
        REQUIRE(glyph.horizontal_advance == ((face->glyph->advance.x + 32) >> 6));
        adapter.free_glyph_bitmap(bitmap);
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
}
