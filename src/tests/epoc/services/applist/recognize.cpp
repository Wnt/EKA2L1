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

#include <common/buffer.h>
#include <services/applist/applist.h>

#include <string>
#include <vector>

using namespace eka2l1;

namespace {
    data_recog_result recognize(std::vector<std::uint8_t> data, const std::u16string &name) {
        common::ro_buf_stream stream(data.empty() ? nullptr : data.data(), data.size());
        return applist_server::recognize_data_impl(stream, name);
    }

    std::string type_of(data_recog_result result) {
        return result.type_.type_name_.to_std_string(nullptr);
    }

    std::vector<std::uint8_t> bytes(const std::string &content) {
        return std::vector<std::uint8_t>(content.begin(), content.end());
    }
}

TEST_CASE("recognizer_reports_apparc_confidence_values", "applist") {
    // TRecognitionConfidence (apmrec.h). A client compares these numerically, and
    // EPossible is zero rather than the middle of the scale, so the fallback below
    // has to sit above it or it reads as "nothing recognised this".
    REQUIRE(data_recognition_confidence_certain == 0x7FFFFFFF);
    REQUIRE(data_recognition_confidence_probable == 100);
    REQUIRE(data_recognition_confidence_possible == 0);
    REQUIRE(data_recognition_confidence_unlikely == -100);

    data_recog_result unknown = recognize(bytes("nothing in particular"), u"c:\\data\\thing.bin");
    REQUIRE(type_of(unknown) == "application/octet-stream");
    REQUIRE(unknown.confidence_rating_ > data_recognition_confidence_possible);
}

TEST_CASE("recognizer_reads_the_name_for_web_content", "applist") {
    // S60's web recognizer reports XHTML as text/html; it has no separate
    // application/xhtml+xml type. The Store's local front page is an .xhtml file
    // whose bytes say nothing, so the name is the only thing to go on.
    const std::vector<std::uint8_t> page = bytes("<!DOCTYPE html><html></html>");

    for (const std::u16string &name : { std::u16string(u"e:\\showroom\\front.xhtml"),
             std::u16string(u"e:\\showroom\\front.html"), std::u16string(u"e:\\a.HTM"),
             std::u16string(u"e:\\a.shtml") }) {
        data_recog_result result = recognize(page, name);
        REQUIRE(type_of(result) == "text/html");
        REQUIRE(result.confidence_rating_ == data_recognition_confidence_probable);
    }

    data_recog_result xml = recognize(page, u"e:\\feed.xml");
    REQUIRE(type_of(xml) == "text/xml");
}

TEST_CASE("recognizer_reads_the_magic_for_media", "applist") {
    // RIFF, a size, then the WAVE form type at bytes 8 to 11.
    std::vector<std::uint8_t> wave = { 'R', 'I', 'F', 'F', 0x24, 0x08, 0, 0, 'W', 'A', 'V', 'E' };
    data_recog_result wav = recognize(wave, u"c:\\sound.dat");
    REQUIRE(type_of(wav) == "audio/wav");
    REQUIRE(wav.confidence_rating_ == data_recognition_confidence_certain);

    // An ID3 tag, and a bare MPEG frame header.
    REQUIRE(type_of(recognize({ 'I', 'D', '3', 0x03, 0, 0, 0, 0, 0, 0, 0, 0 }, u"c:\\a.dat")) == "audio/mpeg");
    REQUIRE(type_of(recognize({ 0xFF, 0xFB, 0x90, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 }, u"c:\\a.dat")) == "audio/mpeg");

    // The three Flash signatures: uncompressed, zlib and LZMA.
    for (const char first : { 'F', 'C', 'Z' }) {
        std::vector<std::uint8_t> flash = { static_cast<std::uint8_t>(first), 'W', 'S', 0x0A, 0, 0, 0, 0, 0, 0, 0, 0 };
        REQUIRE(type_of(recognize(flash, u"c:\\movie.dat")) == "application/x-shockwave-flash");
    }

    // ftyp sits at byte 4, so the brand runs from 4 to 11.
    REQUIRE(type_of(recognize({ 0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'm', 'p', '4', '2' }, u"c:\\clip.dat")) == "video/mp4");
}

TEST_CASE("recognizer_knows_the_series80_media_types", "applist") {
    // Series 80's Images, Music player and RealPlayer keep only the files whose type they
    // handle, so each of their formats has to come back as the MIME type the ROM names.
    REQUIRE(type_of(recognize({ 0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F', 'I', 'F', 0, 1 }, u"c:\\a.dat")) == "image/jpeg");
    REQUIRE(type_of(recognize({ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D }, u"c:\\a.dat")) == "image/png");
    REQUIRE(type_of(recognize(bytes("GIF89a......"), u"c:\\a.dat")) == "image/gif");
    REQUIRE(type_of(recognize(bytes("BM6.......6."), u"c:\\a.dat")) == "image/bmp");
    REQUIRE(type_of(recognize({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2 }, u"c:\\a.dat")) == "audio/midi");
    REQUIRE(type_of(recognize(bytes("#!AMR\n......"), u"c:\\a.dat")) == "audio/amr");
    REQUIRE(type_of(recognize({ 0xFF, 0xF1, 0x50, 0x80, 0, 0, 0, 0, 0, 0, 0, 0 }, u"c:\\a.dat")) == "audio/aac");
    REQUIRE(type_of(recognize({ 0, 0, 0, 0x14, 'f', 't', 'y', 'p', '3', 'g', 'p', '4' }, u"c:\\a.dat")) == "video/3gpp");
    REQUIRE(type_of(recognize({ 0, 0, 0, 0x14, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm' }, u"c:\\a.dat")) == "video/mp4");
    REQUIRE(type_of(recognize({ '.', 'R', 'M', 'F', 0, 0, 0, 0x12, 0, 1, 0, 0 }, u"c:\\a.dat")) == "application/vnd.rn-realmedia");

    // Bytes that say nothing: the name decides, one step above EPossible.
    data_recog_result named = recognize(bytes("nothing at all"), u"c:\\My files\\clip.3GP");
    REQUIRE(type_of(named) == "video/3gpp");
    REQUIRE(named.confidence_rating_ > data_recognition_confidence_possible);
    REQUIRE(type_of(recognize(bytes("nothing at all"), u"c:\\tune.mid")) == "audio/midi");
}
