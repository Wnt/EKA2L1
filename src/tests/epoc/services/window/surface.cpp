/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <catch2/catch.hpp>
#include <drivers/graphics/graphics.h>
#include <services/window/surface.h>
#include <services/window/bitmap_cache.h>
#include <services/window/classes/gstore.h>
#include <services/window/classes/winbase.h>
#include <services/window/screen.h>
#include <services/fbs/bitmap.h>
#include <services/fbs/palette.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <thread>

using namespace eka2l1;

TEMPLATE_TEST_CASE("GDI command storage aligns every payload", "[window_surface],[gdi_store]",
    epoc::gdi_store_command_draw_rect_data,
    epoc::gdi_store_command_draw_line_data,
    epoc::gdi_store_command_draw_polygon_data,
    epoc::gdi_store_command_draw_text_data,
    epoc::gdi_store_command_draw_bitmap_data,
    epoc::gdi_store_command_update_texture_data,
    epoc::gdi_store_command_set_clip_rect_single_data,
    epoc::gdi_store_command_set_clip_rect_multiple_data) {
    epoc::gdi_store_command commands[2];
    for (auto &command : commands) {
        REQUIRE(reinterpret_cast<std::uintptr_t>(command.data_) % alignof(TestType) == 0);
        REQUIRE(static_cast<void *>(&command.get_data_struct<TestType>()) == command.data_);
        const auto &stored = command;
        REQUIRE(static_cast<const void *>(&stored.get_data_struct_const<TestType>()) == command.data_);
    }
}

namespace {
    class surface_driver : public drivers::graphics_driver {
        drivers::handle next_ = 1;
        drivers::handle bound_ = 0;
        bool blend_ = false;
        vec4 brush_{ 0, 0, 0, 0 };
        drivers::blend_factor factors_[4]{};

        void paint(const std::vector<std::uint8_t> &source) {
            auto &destination = images[bound_];
            if (!blend_) {
                destination = source;
                return;
            }
            destination.resize(source.size());
            for (std::size_t i = 0; i < source.size(); ++i) {
                const double alpha = source[i / 4 * 4 + 3] / 255.0;
                const auto factor = [alpha](drivers::blend_factor value) {
                    if (value == drivers::blend_factor::one) return 1.0;
                    if (value == drivers::blend_factor::frag_out_alpha) return alpha;
                    if (value == drivers::blend_factor::one_minus_frag_out_alpha) return 1.0 - alpha;
                    return 0.0;
                };
                const int offset = i % 4 == 3 ? 2 : 0;
                destination[i] = static_cast<std::uint8_t>(std::min(255.0,
                    source[i] * factor(factors_[offset]) + destination[i] * factor(factors_[offset + 1])));
            }
        }

    public:
        std::map<drivers::handle, std::vector<std::uint8_t>> images;
        std::size_t uploads = 0;
        bool invalid_use = false;

        surface_driver() : graphics_driver(drivers::graphic_api::opengl) {}
        void run() override {}
        void abort() override {}
        void update_bitmap(drivers::handle, std::size_t, const vec2 &, const vec2 &, const void *, std::size_t) override {}
        void set_viewport(const rect &) override {}
        void update_surface(void *) override {}
        void update_surface_size(const vec2 &) override {}
        void set_upscale_shader(const std::string &) override {}
        std::string get_active_upscale_shader() const override { return {}; }
        bool support_extension(drivers::graphics_driver_extension) override { return false; }
        bool query_extension_value(drivers::graphics_driver_extension_query, void *) override { return false; }

        void submit_command_list(drivers::command_list &commands) override {
            for (std::size_t i = 0; i < commands.size_; ++i) {
                auto &command = commands.base_[i];
                const auto handle = command.data_[0];
                switch (command.opcode_) {
                case drivers::graphics_driver_set_feature:
                    if (static_cast<std::uint32_t>(handle) == static_cast<std::uint32_t>(drivers::graphics_feature::blend)) {
                        blend_ = handle >> 32;
                    }
                    break;
                case drivers::graphics_driver_blend_formula:
                    factors_[0] = static_cast<drivers::blend_factor>(static_cast<std::uint32_t>(command.data_[1]));
                    factors_[1] = static_cast<drivers::blend_factor>(command.data_[1] >> 32);
                    factors_[2] = static_cast<drivers::blend_factor>(static_cast<std::uint32_t>(command.data_[2]));
                    factors_[3] = static_cast<drivers::blend_factor>(command.data_[2] >> 32);
                    break;
                case drivers::graphics_driver_set_brush_color:
                    brush_ = { static_cast<int>(handle), static_cast<int>(handle >> 32),
                        static_cast<int>(command.data_[1]), static_cast<int>(command.data_[1] >> 32) };
                    break;
                case drivers::graphics_driver_draw_rectangle:
                    paint({ static_cast<std::uint8_t>(brush_.x), static_cast<std::uint8_t>(brush_.y),
                        static_cast<std::uint8_t>(brush_.z), static_cast<std::uint8_t>(brush_.w) });
                    break;
                case drivers::graphics_driver_create_bitmap:
                case drivers::graphics_driver_create_texture: {
                    const auto created = next_++;
                    images[created] = {};
                    const auto slot = command.opcode_ == drivers::graphics_driver_create_bitmap ? 2 : 8;
                    *reinterpret_cast<drivers::handle*>(command.data_[slot]) = created;
                    break;
                }
                case drivers::graphics_driver_bind_bitmap:
                    bound_ = handle;
                    break;
                case drivers::graphics_driver_update_bitmap:
                case drivers::graphics_driver_update_texture: {
                    const auto *data = reinterpret_cast<const std::uint8_t*>(command.data_[1]);
                    invalid_use |= !images.count(handle);
                    images[handle].assign(data, data + command.data_[2]);
                    delete[] reinterpret_cast<const char*>(data);
                    ++uploads;
                    break;
                }
                case drivers::graphics_driver_draw_bitmap:
                    invalid_use |= !images.count(handle);
                    if (bound_) {
                        paint(images[handle]);
                    }
                    break;
                case drivers::graphics_driver_destroy_bitmap:
                case drivers::graphics_driver_destroy_object:
                    invalid_use |= images.erase(handle) != 1;
                    break;
                default:
                    break;
                }
                if (command.status_) {
                    *command.status_ = 0;
                }
            }
            delete[] commands.base_;
        }
    };

    void submit(surface_driver &driver, drivers::graphics_command_builder &builder) {
        auto commands = builder.retrieve_command_list();
        driver.submit_command_list(commands);
    }
}

TEST_CASE("Same-size screen modes switch color depth and discard the old DSA texture", "[screen_mode]") {
    epoc::config::screen config{};
    config.modes = {
        { 0, 1, { 176, 208 }, 0, "", epoc::display_mode::color16mu, epoc::display_mode::color64k },
        { 0, 2, { 176, 208 }, 0, "", epoc::display_mode::color64k, epoc::display_mode::color64k }
    };
    epoc::screen screen(0, config);
    surface_driver driver;

    CHECK(screen.disp_mode == epoc::display_mode::color16mu);
    CHECK(screen.screen_buffer_byte_width() == 704);
    CHECK(screen.dsa_disp_mode == epoc::display_mode::color64k);

    screen.dsa_disp_mode = epoc::display_mode::color16mu;
    const auto old_texture = drivers::create_bitmap(&driver, { 208, 208 }, 32);
    screen.dsa_texture = old_texture;
    screen.set_screen_mode(nullptr, &driver, 1);
    CHECK(screen.crr_mode == 1);
    CHECK(screen.disp_mode == epoc::display_mode::color64k);
    CHECK(screen.screen_buffer_byte_width() == 352);
    CHECK(screen.dsa_disp_mode == epoc::display_mode::color64k);
    CHECK(screen.dsa_texture == 0);
    CHECK(driver.images.count(old_texture) == 0);

    screen.set_screen_mode(nullptr, &driver, 0);
    CHECK(screen.disp_mode == epoc::display_mode::color16mu);
    CHECK(screen.screen_buffer_byte_width() == 704);
    CHECK(screen.dsa_disp_mode_initial == epoc::display_mode::color64k);

    screen.dsa_disp_mode = epoc::display_mode::color16mu;
    screen.set_screen_mode(nullptr, &driver, 0);
    CHECK(screen.dsa_disp_mode == epoc::display_mode::color16mu);
    screen.set_screen_mode(nullptr, &driver, -1);
    screen.set_screen_mode(nullptr, &driver, 2);
    CHECK(screen.crr_mode == 0);
    CHECK(screen.disp_mode == epoc::display_mode::color16mu);

    screen.deinit(&driver);
    CHECK_FALSE(driver.invalid_use);
}

TEST_CASE("Window background replacement is independent of producer publication", "[window_surface]") {
    auto gl = std::make_shared<epoc::window_surface>();
    auto movie = std::make_shared<epoc::window_surface>();
    epoc::window_surface_attachment window;
    const std::uint8_t white[] = { 255, 255, 255, 255 };
    const std::uint8_t red[] = { 255, 0, 0, 255 };

    window.attach(gl, {});
    window.attach(movie, {});
    REQUIRE(movie->publish_pixels(red, sizeof(red), { 1, 1 }));
    REQUIRE(gl->publish_pixels(white, sizeof(white), { 1, 1 }));
    REQUIRE(window.surface == movie);
    REQUIRE(window.changed());
    REQUIRE_FALSE(window.detach(gl));
    REQUIRE(window.surface == movie);
    REQUIRE(window.detach(movie));
    REQUIRE_FALSE(window.surface);
    REQUIRE(gl->publish_pixels(white, sizeof(white), { 1, 1 }));
    REQUIRE_FALSE(window.surface);
    window.attach(gl, {});
    REQUIRE(window.surface == gl);
}

TEST_CASE("Surface mailbox owns and coalesces complete frames", "[window_surface]") {
    epoc::window_surface surface;
    std::vector<std::uint8_t> first(16, 17);
    REQUIRE(surface.publish_pixels(first.data(), first.size(), { 2, 2 }));
    std::fill(first.begin(), first.end(), 42);
    auto pending = surface.take_pixels();
    REQUIRE(pending);
    REQUIRE(pending->rgba == std::vector<std::uint8_t>(16, 17));
    REQUIRE_FALSE(surface.take_pixels());

    REQUIRE(surface.publish_pixels(first.data(), first.size(), { 2, 2 }));
    std::fill(first.begin(), first.end(), 91);
    REQUIRE(surface.publish_pixels(first.data(), first.size(), { 2, 2 }));
    pending = surface.take_pixels();
    REQUIRE(pending->revision == 3);
    REQUIRE(pending->rgba == first);
    REQUIRE_FALSE(surface.publish_pixels(first.data(), 15, { 2, 2 }));
    REQUIRE_FALSE(surface.publish_pixels(nullptr, 16, { 2, 2 }));
    REQUIRE_FALSE(surface.publish_pixels(first.data(), 16, { -1, 2 }));
    REQUIRE_FALSE(surface.publish_pixels(first.data(), 16, { std::numeric_limits<int>::max(), std::numeric_limits<int>::max() }));
    REQUIRE(surface.revision() == 3);
}

TEST_CASE("Surface clipping preserves the mapping instead of stretching into the clip", "[window_surface]") {
    epoc::surface_configuration config;
    config.extent = rect({ 10, 20 }, { 200, 100 });
    config.clip = rect({ 60, 20 }, { 50, 100 });
    auto placement = epoc::place_surface(config, rect({ 30, 40 }, { 300, 200 }), { 640, 320 }, 2.0f, 0);
    REQUIRE(placement.destination == rect({ 80, 120 }, { 400, 200 }));
    REQUIRE(placement.source == rect({ 0, 0 }, { 640, 320 }));
    REQUIRE(placement.clip == rect({ 90, 60 }, { 50, 100 }));

    config.rotation = 90;
    placement = epoc::place_surface(config, rect({ 30, 40 }, { 300, 200 }), { 640, 320 }, 2.0f, 0);
    REQUIRE(placement.destination == rect({ 480, 120 }, { 200, 400 }));
    REQUIRE(placement.clip == rect({ 90, 60 }, { 50, 100 }));
    REQUIRE(placement.rotation == 90);

    config.clip = rect({ 500, 500 }, { 10, 10 });
    placement = epoc::place_surface(config, rect({ 30, 40 }, { 300, 200 }), { 640, 320 }, 1.0f, 0);
    REQUIRE(placement.clip.empty());
}

TEST_CASE("A presented EGL image survives further writes and producer destruction", "[window_surface]") {
    surface_driver driver;
    const auto drawing = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    driver.images[drawing] = { 255, 0, 0, 255 };
    auto surface = std::make_shared<epoc::window_surface>();
    REQUIRE(surface->publish_bitmap(&driver, drawing, { 1, 1 }));
    driver.images[drawing] = { 255, 255, 255, 255 };

    epoc::window_surface_attachment window;
    window.attach(surface, {});
    surface.reset();
    drivers::graphics_command_builder builder;
    const auto presented = window.surface->prepare(&driver, builder);
    REQUIRE(driver.images[presented] == std::vector<std::uint8_t>{ 255, 0, 0, 255 });
    REQUIRE(presented != drawing);
    submit(driver, builder);
    window.surface.reset();
    REQUIRE_FALSE(driver.images.count(presented));
    REQUIRE(driver.images.count(drawing));
    REQUIRE_FALSE(driver.invalid_use);
}

TEST_CASE("Multiple targets upload one video frame and retire replaced textures in order", "[window_surface]") {
    surface_driver driver;
    auto surface = std::make_shared<epoc::window_surface>();
    const std::uint8_t red[] = { 255, 0, 0, 255 };
    REQUIRE(surface->publish_pixels(red, sizeof(red), { 1, 1 }));
    drivers::graphics_command_builder builder;
    const auto first = surface->prepare(&driver, builder);
    builder.draw_bitmap(first, 0, rect({ 0, 0 }, { 1, 1 }), {});
    REQUIRE(surface->prepare(&driver, builder) == first);
    submit(driver, builder);
    REQUIRE(driver.uploads == 1);

    const std::vector<std::uint8_t> bigger(16, 128);
    REQUIRE(surface->publish_pixels(bigger.data(), bigger.size(), { 2, 2 }));
    builder.draw_bitmap(first, 0, rect({ 0, 0 }, { 1, 1 }), {});
    const auto second = surface->prepare(&driver, builder);
    builder.draw_bitmap(second, 0, rect({ 0, 0 }, { 2, 2 }), {});
    submit(driver, builder);
    REQUIRE(first != second);
    REQUIRE_FALSE(driver.images.count(first));
    REQUIRE(driver.images[second] == bigger);
    surface.reset();
    REQUIRE(driver.images.empty());
    REQUIRE_FALSE(driver.invalid_use);
}

TEST_CASE("Publishing continues safely while window attachments are replaced and removed", "[window_surface]") {
    auto source = std::make_shared<epoc::window_surface>();
    std::atomic<bool> done{ false };
    std::thread producer([source, &done] {
        for (unsigned i = 0; i < 10000; ++i) {
            const std::vector<std::uint8_t> bytes(256, static_cast<std::uint8_t>(i));
            source->publish_pixels(bytes.data(), bytes.size(), { 8, 8 });
        }
        done = true;
    });
    std::uint64_t revision = 0;
    bool torn_frame = false;
    while (!done) {
        epoc::window_surface_attachment window;
        window.attach(source, {});
        auto replacement = std::make_shared<epoc::window_surface>();
        window.attach(replacement, {});
        window.detach(source);
        if (auto pixels = source->take_pixels()) {
            torn_frame |= pixels->revision <= revision;
            revision = pixels->revision;
            torn_frame |= !std::all_of(pixels->rgba.begin(), pixels->rgba.end(), [&](auto value) { return value == pixels->rgba.front(); });
        }
    }
    producer.join();
    REQUIRE_FALSE(torn_frame);
    REQUIRE(source->revision() == 10000);
}

TEST_CASE("Retained GDI pixels preserve alpha and redraw clears expose the surface", "[window_surface]") {
    surface_driver driver;
    epoc::bitmap_cache cache(nullptr);
    drivers::graphics_command_builder builder;
    const auto ui = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    builder.bind_bitmap(ui);
    common::region clip;
    clip.add_rect(rect({ 0, 0 }, { 1, 1 }));
    epoc::gdi_command_builder gdi(&driver, builder, cache, drivers::filter_option::nearest,
        { 0, 0 }, 1.0f, clip, true);
    epoc::gdi_store_command rectangle;
    rectangle.opcode_ = epoc::gdi_store_command_draw_rect;
    auto &data = rectangle.get_data_struct<epoc::gdi_store_command_draw_rect_data>();
    data.rect_ = rect({ 0, 0 }, { 1, 1 });
    data.color_ = { 255, 0, 0, 128 };
    gdi.build_single_command(rectangle);
    submit(driver, builder);
    REQUIRE(driver.images[ui] == std::vector<std::uint8_t>{ 128, 0, 0, 128 });

    const auto screen = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    driver.images[screen] = { 0, 0, 255, 255 };
    builder.bind_bitmap(screen);
    builder.blend_formula(drivers::blend_equation::add, drivers::blend_equation::add,
        drivers::blend_factor::one, drivers::blend_factor::one_minus_frag_out_alpha,
        drivers::blend_factor::one, drivers::blend_factor::one_minus_frag_out_alpha);
    builder.draw_bitmap(ui, 0, rect({ 0, 0 }, { 1, 1 }), {});
    submit(driver, builder);
    REQUIRE(driver.images[screen] == std::vector<std::uint8_t>{ 128, 0, 127, 255 });

    builder.bind_bitmap(ui);
    data.color_ = { 0, 0, 0, 0 };
    gdi.build_single_command(rectangle);
    submit(driver, builder);
    REQUIRE(driver.images[ui] == std::vector<std::uint8_t>{ 0, 0, 0, 0 });
}

TEST_CASE("Initial GDI replay consumes pending uploads without repeating pixel draws", "[window_surface]") {
    surface_driver driver;
    epoc::bitmap_cache cache(nullptr);
    drivers::graphics_command_builder builder;
    const auto ui = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    const auto bitmap = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    driver.images[ui] = { 0, 0, 0, 0 };
    builder.bind_bitmap(ui);
    common::region clip;
    clip.add_rect(rect({ 0, 0 }, { 1, 1 }));
    epoc::gdi_command_builder gdi(&driver, builder, cache, drivers::filter_option::nearest,
        { 0, 0 }, 1.0f, clip, true);

    epoc::gdi_store_command_segment pending;
    epoc::gdi_store_command update;
    update.opcode_ = epoc::gdi_store_command_update_texture;
    auto &upload = update.get_data_struct<epoc::gdi_store_command_update_texture_data>();
    upload = {};
    upload.handle_ = bitmap;
    upload.texture_data_ = new char[4]{ 127, 0, 0, 127 };
    upload.texture_size_ = 4;
    upload.dim_ = { 1, 1 };
    upload.pixel_per_line_ = 1;
    pending.add_command(update);

    epoc::gdi_store_command rectangle;
    rectangle.opcode_ = epoc::gdi_store_command_draw_rect;
    auto &fill = rectangle.get_data_struct<epoc::gdi_store_command_draw_rect_data>();
    fill.rect_ = rect({ 0, 0 }, { 1, 1 });
    fill.color_ = { 0, 255, 0, 255 };
    pending.add_command(rectangle);
    gdi.build_texture_updates(pending);
    submit(driver, builder);
    REQUIRE(driver.images[bitmap] == std::vector<std::uint8_t>{ 127, 0, 0, 127 });
    REQUIRE(driver.images[ui] == std::vector<std::uint8_t>{ 0, 0, 0, 0 });
    REQUIRE(driver.uploads == 1);
}

TEST_CASE("Retained GDI accepts both straight and premultiplied alpha bitmaps", "[window_surface]") {
    surface_driver driver;
    epoc::bitmap_cache cache(nullptr);
    drivers::graphics_command_builder builder;
    const auto ui = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    const auto bitmap = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    builder.bind_bitmap(ui);
    common::region clip;
    clip.add_rect(rect({ 0, 0 }, { 1, 1 }));
    epoc::gdi_command_builder gdi(&driver, builder, cache, drivers::filter_option::nearest,
        { 0, 0 }, 1.0f, clip, true);

    epoc::bitwise_bitmap source{};
    source.header_.size_pixels = object_size(1, 1);
    epoc::gdi_store_command draw;
    draw.opcode_ = epoc::gdi_store_command_draw_bitmap;
    auto &data = draw.get_data_struct<epoc::gdi_store_command_draw_bitmap_data>();
    data = {};
    data.main_fbs_bitmap_ = &source;
    data.main_drv_ = bitmap;
    data.gdi_flags_ = epoc::GDI_STORE_COMMAND_MAIN_RAW;
    data.dest_rect_ = rect({ 0, 0 }, { 1, 1 });
    data.source_rect_ = data.dest_rect_;

    for (const auto mode : { epoc::display_mode::color16ma, epoc::display_mode::color16map }) {
        source.settings_.current_display_mode(mode);
        driver.images[bitmap] = { static_cast<std::uint8_t>(mode == epoc::display_mode::color16ma ? 255 : 128), 0, 0, 128 };
        gdi.build_single_command(draw);
        submit(driver, builder);
        REQUIRE(driver.images[ui] == std::vector<std::uint8_t>{ 128, 0, 0, 128 });
    }
}

TEST_CASE("A redraw of a rectangle that covers no pixel leaves no segment behind", "[gdi_store]") {
    // BeginRedraw on a 0x34 spacer window, as S80 Sheet does hundreds of times a second while the
    // invalid region is stuck: each pass used to leave one more segment that nothing could remove.
    epoc::gdi_store_command_collection store;

    for (int i = 0; i < 64; i++) {
        store.add_new_segment(eka2l1::rect({ 0, 0 }, { 0, 34 }), epoc::gdi_store_command_segment_pending_redraw);
        store.promote_last_segment();
    }

    REQUIRE(store.get_segments().empty());
    REQUIRE(store.get_current_segment() == nullptr);

    // A real redraw still replaces the one before it.
    store.add_new_segment(eka2l1::rect({ 0, 0 }, { 10, 10 }), epoc::gdi_store_command_segment_pending_redraw);
    store.promote_last_segment();
    store.add_new_segment(eka2l1::rect({ 0, 0 }, { 10, 10 }), epoc::gdi_store_command_segment_pending_redraw);
    store.promote_last_segment();

    REQUIRE(store.get_segments().size() == 1);
    REQUIRE(store.get_segments()[0]->type_ == epoc::gdi_store_command_segment_redraw);
}

TEST_CASE("A masked blit reads an EColor4K mask from the channel its texture keeps red in", "[gdi_store]") {
    // EColor4K uploads as RGBA4444 holding 0RGB and the driver swizzles it (G, B, A, 1): red sits in
    // the texture's green channel, and the texture's red channel is the zero top nibble. The mask
    // shader reads red, so a 12bpp mask read through (R, G, B, R) masked nothing, and the Series 80
    // icons that carry EColor4K masks (drawn inverted) came out as solid key-colour boxes.
    surface_driver driver;
    epoc::bitmap_cache cache(nullptr);
    drivers::graphics_command_builder builder;
    const auto ui = drivers::create_bitmap(&driver, { 1, 1 }, 32);
    const auto source_texture = drivers::create_bitmap(&driver, { 1, 1 }, 16);
    const auto mask_texture = drivers::create_bitmap(&driver, { 1, 1 }, 12);
    builder.bind_bitmap(ui);
    common::region clip;
    clip.add_rect(rect({ 0, 0 }, { 1, 1 }));
    epoc::gdi_command_builder gdi(&driver, builder, cache, drivers::filter_option::nearest,
        { 0, 0 }, 1.0f, clip, false);

    epoc::bitwise_bitmap source{};
    source.uid_ = epoc::bitwise_bitmap_uid;
    source.header_.size_pixels = object_size(1, 1);
    source.header_.bit_per_pixels = 16;
    source.settings_.current_display_mode(epoc::display_mode::color64k);

    epoc::bitwise_bitmap mask = source;

    epoc::gdi_store_command draw;
    draw.opcode_ = epoc::gdi_store_command_draw_bitmap;
    auto &data = draw.get_data_struct<epoc::gdi_store_command_draw_bitmap_data>();
    data = {};
    data.main_fbs_bitmap_ = &source;
    data.mask_fbs_bitmap_ = &mask;
    data.main_drv_ = source_texture;
    data.mask_drv_ = mask_texture;
    data.gdi_flags_ = epoc::GDI_STORE_COMMAND_MAIN_RAW | epoc::GDI_STORE_COMMAND_MASK_RAW
        | epoc::GDI_STORE_COMMAND_INVERT_MASK | epoc::GDI_STORE_COMMAND_BLIT;
    data.source_rect_ = rect({ 0, 0 }, { 1, 1 });

    using swizzle = drivers::channel_swizzle;
    using swizzle_set = std::array<swizzle, 4>;

    const auto mask_swizzles = [&]() {
        std::vector<swizzle_set> found;
        gdi.build_single_command(draw);
        auto commands = builder.retrieve_command_list();
        for (std::size_t i = 0; i < commands.size_; ++i) {
            const auto &command = commands.base_[i];
            if ((command.opcode_ == drivers::graphics_driver_set_swizzle) && (command.data_[0] == mask_texture)) {
                swizzle_set channels;
                drivers::unpack_u64_to_2u32(command.data_[1], channels[0], channels[1]);
                drivers::unpack_u64_to_2u32(command.data_[2], channels[2], channels[3]);
                found.push_back(channels);
            }
        }
        delete[] commands.base_;
        return found;
    };

    SECTION("EColor4K: red from the texture's green channel, then the driver's own order back") {
        mask.header_.bit_per_pixels = 12;
        mask.settings_.current_display_mode(epoc::display_mode::color4k);

        const auto found = mask_swizzles();
        REQUIRE(found.size() == 2);
        REQUIRE(found[0] == swizzle_set{ swizzle::green, swizzle::blue, swizzle::alpha, swizzle::green });
        REQUIRE(found[1] == swizzle_set{ swizzle::green, swizzle::blue, swizzle::alpha, swizzle::one });
    }

    SECTION("EColor64K keeps red in red") {
        const auto found = mask_swizzles();
        REQUIRE(found.size() == 2);
        REQUIRE(found[0] == swizzle_set{ swizzle::red, swizzle::green, swizzle::blue, swizzle::red });
        REQUIRE(found[1] == swizzle_set{ swizzle::red, swizzle::green, swizzle::blue, swizzle::alpha });
    }
}

TEST_CASE("Texture uploads reach the driver without any window being drawn", "[gdi_store]") {
    // The bitmap cache records a texture as current as soon as it hands out the upload. The upload used to
    // ride in the pending segment of the window that drew the bitmap first; a hidden window never builds
    // it, and every later user of the bitmap drew the empty texture (the S80 choice-list scroll bar came
    // out solid black). The upload now goes to the driver on its own.
    surface_driver driver;
    epoc::bitmap_cache cache(nullptr);
    const auto texture = drivers::create_bitmap(&driver, { 1, 1 }, 32);

    epoc::gdi_store_command update;
    update.opcode_ = epoc::gdi_store_command_update_texture;
    auto &upload = update.get_data_struct<epoc::gdi_store_command_update_texture_data>();
    upload = {};
    upload.handle_ = texture;
    upload.texture_data_ = new char[4]{ 1, 2, 3, 4 };
    upload.texture_size_ = 4;
    upload.dim_ = { 1, 1 };
    upload.pixel_per_line_ = 1;

    epoc::gdi_store_command nothing;

    epoc::gdi_submit_texture_updates(&driver, cache, { &update, &nothing });
    REQUIRE(driver.uploads == 1);
    REQUIRE(driver.images[texture] == std::vector<std::uint8_t>{ 1, 2, 3, 4 });
    REQUIRE_FALSE(driver.invalid_use);

    // No update among them: nothing is sent.
    epoc::gdi_submit_texture_updates(&driver, cache, { &nothing, nullptr });
    REQUIRE(driver.uploads == 1);
}

TEST_CASE("A masked blit with a brush fills the blitted part of the source", "[gdi_store]") {
    // BitBltMasked(pos, 10x32 bitmap, (0,0)-(10,32), ...) as the S80 scroll bar does.
    REQUIRE(epoc::masked_blit_brush_area({ 0, 33 }, rect({ 0, 0 }, { 10, 32 }), { 10, 32 }) == rect({ 0, 33 }, { 10, 32 }));

    // A part of the source only.
    REQUIRE(epoc::masked_blit_brush_area({ 5, 5 }, rect({ 0, 0 }, { 10, 14 }), { 10, 32 }) == rect({ 5, 5 }, { 10, 14 }));

    // Never past the bitmap: a source rectangle that runs off it is cut back.
    REQUIRE(epoc::masked_blit_brush_area({ 1, 0 }, rect({ 4, 0 }, { 136, 22 }), { 103, 22 }) == rect({ 1, 0 }, { 99, 22 }));

    // An empty source rectangle blits the whole bitmap.
    REQUIRE(epoc::masked_blit_brush_area({ 2, 3 }, rect({ 0, 0 }, { 0, 0 }), { 13, 11 }) == rect({ 2, 3 }, { 13, 11 }));
}

namespace {
    epoc::gdi_store_command opaque_fill(const rect &area, const int alpha = 255) {
        epoc::gdi_store_command command;
        command.opcode_ = epoc::gdi_store_command_draw_rect;
        auto &data = command.get_data_struct<epoc::gdi_store_command_draw_rect_data>();
        data.rect_ = area;
        data.color_ = { 255, 255, 255, alpha };
        return command;
    }

    epoc::gdi_store_command_segment *redraw(epoc::gdi_store_command_collection &store, const rect &area,
        std::initializer_list<epoc::gdi_store_command> commands, const bool background_clears) {
        auto *segment = store.add_new_segment(area, epoc::gdi_store_command_segment_pending_redraw);
        for (auto command : commands) {
            segment->add_command(command);
        }
        store.promote_last_segment(background_clears);
        return segment;
    }
}

TEST_CASE("A redraw of a window without background keeps what it does not paint", "[gdi_store]") {
    // Series 80 Contacts: the card pane first paints itself white, later partial redraws of the same
    // (SetNoBackgroundColor) window leave a band unpainted. On the device the old pixels stay; dropping
    // the older segment made the band black at the next recomposition.
    const rect window({ 0, 0 }, { 100, 100 });

    SECTION("with a background colour the redraw replaces everything under it") {
        epoc::gdi_store_command_collection store;
        redraw(store, window, { opaque_fill(window) }, true);
        redraw(store, window, { opaque_fill(rect({ 0, 0 }, { 100, 50 })) }, true);
        REQUIRE(store.get_segments().size() == 1);
    }

    SECTION("without one the older segment stays under the unpainted part") {
        epoc::gdi_store_command_collection store;
        auto *first = redraw(store, window, { opaque_fill(window) }, false);
        redraw(store, window, { opaque_fill(rect({ 0, 0 }, { 100, 50 })) }, false);
        REQUIRE(store.get_segments().size() == 2);
        REQUIRE(store.get_segments()[0].get() == first);

        // A later redraw that paints it all over replaces both.
        redraw(store, window, { opaque_fill(window) }, false);
        REQUIRE(store.get_segments().size() == 1);
    }

    SECTION("translucent fills and inverting draws do not cover") {
        epoc::gdi_store_command_collection store;
        redraw(store, window, { opaque_fill(window) }, false);

        epoc::gdi_store_command invert;
        invert.opcode_ = epoc::gdi_store_command_set_draw_mode;
        invert.get_data_struct<epoc::gdi_store_command_set_draw_mode_data>().mode_ = epoc::gdi_draw_mode_notscreen;

        redraw(store, window, { opaque_fill(window, 128), invert, opaque_fill(window) }, false);
        REQUIRE(store.get_segments().size() == 2);
    }

    SECTION("a fill clipped to part of the window covers only that part") {
        epoc::gdi_store_command clip;
        clip.opcode_ = epoc::gdi_store_command_set_clip_rect_single;
        clip.get_data_struct<epoc::gdi_store_command_set_clip_rect_single_data>().clipping_rect_ = rect({ 0, 0 }, { 10, 10 });

        epoc::gdi_store_command_segment segment;
        segment.add_command(clip);
        auto fill = opaque_fill(window);
        segment.add_command(fill);

        const common::region covered = epoc::gdi_store_segment_opaque_coverage(segment);
        REQUIRE(covered.rects_.size() == 1);
        REQUIRE(covered.rects_[0] == rect({ 0, 0 }, { 10, 10 }));
    }

    SECTION("segments kept under redraws stay bounded") {
        epoc::gdi_store_command_collection store;
        for (int i = 0; i < 100; i++) {
            redraw(store, window, {}, false);
        }
        REQUIRE(store.get_segments().size() <= epoc::gdi_store_command_collection::LIMIT_REDRAW_SEGMENTS);
    }
}

TEST_CASE("Bitmaps below a byte per pixel expand to grey levels or EColor16 colours", "[gdi_store]") {
    // EGray4 (2bpp) had no conversion and no driver texture format, so it sampled nothing. Series 80
    // draws icon masks in EGray4 (Opera's Go to address drop-down arrow and globe) with an inverted
    // BitBltMasked: an empty mask let the whole source through, magenta key colour included.
    epoc::bitwise_bitmap bmp{};
    bmp.uid_ = epoc::bitwise_bitmap_uid;
    bmp.header_.size_pixels = object_size(10, 10);

    for (const std::uint32_t bpp : { 1U, 2U, 4U }) {
        bmp.header_.bit_per_pixels = bpp;
        bmp.settings_.current_display_mode(bpp == 1 ? epoc::display_mode::gray2 : (bpp == 2 ? epoc::display_mode::gray4 : epoc::display_mode::gray16));
        REQUIRE(epoc::get_suitable_bpp_for_bitmap(&bmp) == 24);
    }

    bmp.header_.bit_per_pixels = 12;
    bmp.settings_.current_display_mode(epoc::display_mode::color4k);
    REQUIRE(epoc::get_suitable_bpp_for_bitmap(&bmp) == 12);

    REQUIRE(epoc::sub_byte_bitmap_scanline_bytes(10, 2) == 4);
    REQUIRE(epoc::sub_byte_bitmap_scanline_bytes(28, 2) == 8);
    REQUIRE(epoc::sub_byte_bitmap_scanline_bytes(33, 1) == 8);

    const auto pixel = [](const char *out, std::size_t out_stride, int x, int y) {
        const std::uint8_t *p = reinterpret_cast<const std::uint8_t *>(out) + y * out_stride + x * 3;
        return std::array<std::uint8_t, 3>{ p[0], p[1], p[2] };
    };

    SECTION("EGray4: the arrow mask of the Go to address field, row 3 (one scan line = 4 bytes)") {
        // Row "0000000003": nine black pixels (the arrow) and one white (transparent when inverted).
        // Pixel 0 sits in the low bits of byte 0; the white pixel at x = 9 is byte 2, bits 2..3.
        const std::uint8_t rows[8] = { 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x0C, 0x00 };
        std::size_t raw_size = 0;
        char *out = epoc::expand_sub_byte_bitmap_to_24bpp(rows, object_size(10, 2), 2, 4, nullptr, raw_size);
        REQUIRE(out);
        REQUIRE(raw_size == 32 * 2);
        for (int x = 0; x < 10; x++) {
            REQUIRE(pixel(out, 32, x, 0) == std::array<std::uint8_t, 3>{ 0xFF, 0xFF, 0xFF });
            const std::uint8_t level = (x == 9) ? 0xFF : 0x00;
            REQUIRE(pixel(out, 32, x, 1) == std::array<std::uint8_t, 3>{ level, level, level });
        }
        delete[] out;
    }

    SECTION("EGray4 middle levels and EGray16 with an odd width") {
        const std::uint8_t gray4[4] = { 0xE4, 0, 0, 0 }; // 0, 1, 2, 3
        std::size_t raw_size = 0;
        char *out = epoc::expand_sub_byte_bitmap_to_24bpp(gray4, object_size(4, 1), 2, 0, nullptr, raw_size);
        REQUIRE(pixel(out, 12, 0, 0)[0] == 0x00);
        REQUIRE(pixel(out, 12, 1, 0)[0] == 0x55);
        REQUIRE(pixel(out, 12, 2, 0)[0] == 0xAA);
        REQUIRE(pixel(out, 12, 3, 0)[0] == 0xFF);
        delete[] out;

        const std::uint8_t gray16[4] = { 0xF0, 0x07, 0x0A, 0 }; // 0, 15, 7, 0, 10: the fifth pixel is not dropped
        out = epoc::expand_sub_byte_bitmap_to_24bpp(gray16, object_size(5, 1), 4, 0, nullptr, raw_size);
        REQUIRE(raw_size == 16);
        REQUIRE(pixel(out, 16, 1, 0)[1] == 0xFF);
        REQUIRE(pixel(out, 16, 2, 0)[1] == 0x77);
        REQUIRE(pixel(out, 16, 4, 0)[1] == 0xAA);
        delete[] out;
    }

    SECTION("EGray2 packs from the lowest bit") {
        const std::uint8_t mono[4] = { 0x01, 0x80, 0, 0 };
        std::size_t raw_size = 0;
        char *out = epoc::expand_sub_byte_bitmap_to_24bpp(mono, object_size(16, 1), 1, 0, nullptr, raw_size);
        REQUIRE(pixel(out, 48, 0, 0)[0] == 0xFF);
        REQUIRE(pixel(out, 48, 1, 0)[0] == 0x00);
        REQUIRE(pixel(out, 48, 15, 0)[0] == 0xFF);
        delete[] out;
    }

    SECTION("EColor16 indexes its 16-entry palette, both nibbles") {
        const std::uint8_t colour16[4] = { 0x95, 0, 0, 0 }; // index 5 (red), then 9 (blue)
        std::size_t raw_size = 0;
        char *out = epoc::expand_sub_byte_bitmap_to_24bpp(colour16, object_size(2, 1), 4, 0, epoc::color_16_palette.data(), raw_size);
        REQUIRE(pixel(out, 8, 0, 0) == std::array<std::uint8_t, 3>{ 0x00, 0x00, 0xFF }); // B, G, R
        REQUIRE(pixel(out, 8, 1, 0) == std::array<std::uint8_t, 3>{ 0xFF, 0x00, 0x00 });
        delete[] out;
    }

    SECTION("Other depths are not expanded") {
        const std::uint8_t byte[4] = {};
        std::size_t raw_size = 1;
        REQUIRE(epoc::expand_sub_byte_bitmap_to_24bpp(byte, object_size(1, 1), 8, 0, nullptr, raw_size) == nullptr);
        REQUIRE(raw_size == 0);
    }
}
