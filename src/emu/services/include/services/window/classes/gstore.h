/*
 * Copyright (c) 2022 EKA2L1 Team
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

#include <common/vecx.h>
#include <common/region.h>

#include <drivers/graphics/common.h>
#include <drivers/itc.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>

namespace eka2l1::drivers {
    class graphics_driver;
}

namespace eka2l1::epoc {
    class bitmap_cache;
    struct window;

    enum gdi_store_command_opcode : std::uint32_t {
        gdi_store_command_invalid,
        gdi_store_command_draw_rect,
        gdi_store_command_draw_line,
        gdi_store_command_draw_polygon,
        gdi_store_command_draw_bitmap,
        gdi_store_command_draw_text,
        gdi_store_command_set_clip_rect_single,
        gdi_store_command_set_clip_rect_multiple,
        gdi_store_command_disable_clip,
        gdi_store_command_update_texture,
        gdi_store_command_set_draw_mode ///< State: the CGraphicsContext::TDrawMode the following draws use.
    };

    // Whether replaying this opcode puts any pixel on the target. The clipping
    // opcodes only carry state along to the drawing commands after them, so a
    // store holding nothing else can't reproduce what is on screen.
    static inline bool gdi_store_command_draws_pixels(const gdi_store_command_opcode opcode) {
        switch (opcode) {
        case gdi_store_command_draw_rect:
        case gdi_store_command_draw_line:
        case gdi_store_command_draw_polygon:
        case gdi_store_command_draw_bitmap:
        case gdi_store_command_draw_text:
        case gdi_store_command_update_texture:
            return true;

        default:
            return false;
        }
    }

    // CGraphicsContext::TDrawMode (GDI.H): components EInvertScreen 1, EXor 2, EOr 4, EAnd 8,
    // EInvertPen 16, EPenmode 32, EWriteAlpha 64.
    enum gdi_draw_mode : std::uint32_t {
        gdi_draw_mode_invert_screen = 1,
        gdi_draw_mode_xor = 2,
        gdi_draw_mode_or = 4,
        gdi_draw_mode_and = 8,
        gdi_draw_mode_logical_op = gdi_draw_mode_xor | gdi_draw_mode_or | gdi_draw_mode_and,
        gdi_draw_mode_invert_pen = 16,
        gdi_draw_mode_pen = 32,
        gdi_draw_mode_write_alpha = 64,

        gdi_draw_mode_notscreen = gdi_draw_mode_invert_screen,
        gdi_draw_mode_notpen = gdi_draw_mode_invert_pen | gdi_draw_mode_pen
    };

    struct gdi_store_command_set_draw_mode_data {
        std::uint32_t mode_;
    };

    // One pass of a TDrawMode emulated with fixed-function blending: out.rgb = src * src_factor + dst * dst_factor,
    // where src is the colour below (pen or brush, maybe inverted) and dst the pixel already on the target.
    // Colours are 0..1 per channel, so the logical ops are exact for 0/1 channel values and a smooth
    // approximation between them (XOR = S + D - 2SD, AND = SD, OR = S + D - SD).
    struct gdi_draw_mode_pass {
        eka2l1::vec4 color_;
        drivers::blend_factor src_factor_;
        drivers::blend_factor dst_factor_;
    };

    // Expands a draw mode and the pen/brush colour (0..255 per channel) into blend passes. Returns 0 for the
    // plain modes (PEN, WriteAlpha, NOTPEN) that draw with normal blending: color is then only updated
    // (inverted for NOTPEN). At most 2 passes.
    std::uint32_t gdi_expand_draw_mode(const std::uint32_t mode, eka2l1::vec4 &color, gdi_draw_mode_pass *passes);

    /**
     * @brief CFbsBitGc::DrawRect split into pixels that are each painted once.
     *
     * With a pen, the brush fills only the part of the rectangle the pen leaves (`fill`, when `has_fill`),
     * and the pen is four edges that do not overlap: a band `pen_size / 2` outside the rectangle's edge and
     * the rest inside it. Painting every pixel once matters in the logical draw modes (NOTSCREEN, XOR),
     * where a pixel painted twice flips back.
     */
    struct gdi_rect_outline {
        eka2l1::rect fill;
        bool has_fill = false;
        eka2l1::rect edges[4];
        std::size_t edge_count = 0;
    };

    gdi_rect_outline gdi_split_rect_outline(const eka2l1::rect &area, const eka2l1::vec2 &pen_size);

    /**
     * @brief The pixels CFbsBitGc::DrawLine paints for an axis-aligned line: from the start point up to but
     *        not including the end point, whichever way it runs, with the pen of width w centred on it
     *        (w / 2 before the line). A line that starts and ends on the same point is one pen dot.
     */
    eka2l1::rect gdi_axis_line_rect(const eka2l1::vec2 &start, const eka2l1::vec2 &end, const eka2l1::vec2 &pen_size);

    struct gdi_store_command_draw_rect_data {
        eka2l1::vec4 color_;
        eka2l1::rect rect_;
    };

    struct gdi_store_command_draw_line_data {
        eka2l1::vec4 color_;
        drivers::pen_style style_;
        eka2l1::vec2 start_;
        eka2l1::vec2 end_;
        eka2l1::vec2 pen_size_;
    };

    struct gdi_store_command_draw_polygon_data {
        eka2l1::vec4 color_;
        drivers::pen_style style_;
        eka2l1::point *points_;
        std::uint32_t point_count_;
    };

    enum gdi_store_command_draw_text_flags {
        GDI_STORE_COMMAND_TEXT_UNDERLINE = 1 << 0,
        GDI_STORE_COMMAND_TEXT_STRIKETHROUGH = 1 << 1
    };

    struct gdi_store_command_draw_text_data {
        eka2l1::vec4 color_;
        char16_t *string_;
        eka2l1::rect text_box_;
        std::uint32_t alignment_;
        void *fbs_font_ptr_;
        std::uint32_t text_flags_;
    };
    
    struct gdi_store_command_draw_raw_texture_data {
        drivers::handle texture_;
        eka2l1::rect dest_rect_;
        std::uint32_t flags_;
    };

    enum gdi_store_command_draw_bitmap_flags {
        GDI_STORE_COMMAND_INVERT_MASK = 1 << 0,
        GDI_STORE_COMMAND_MAIN_RAW = 1 << 1,
        GDI_STORE_COMMAND_MASK_RAW = 1 << 2,
        GDI_STORE_COMMAND_BLIT = 1 << 3,
        GDI_STORE_COMMAND_TILE = 1 << 4 ///< Source rect may run past the bitmap: repeat it (patterned brush).
    };

    struct gdi_store_command_draw_bitmap_data {
        void *main_fbs_bitmap_;
        void *mask_fbs_bitmap_;

        eka2l1::rect dest_rect_;
        eka2l1::rect source_rect_;

        drivers::handle main_drv_ = 0;
        drivers::handle mask_drv_ = 0;

        std::uint32_t gdi_flags_;
    };

    struct gdi_store_command_update_texture_data {
        drivers::handle handle_ = 0;
        drivers::handle destroy_handle_ = 0;

        void *texture_data_;
        std::size_t texture_size_;
        eka2l1::vec2 dim_;
        std::size_t pixel_per_line_;

        drivers::channel_swizzles swizz_;
        bool do_swizz_ = false;
    };

    struct gdi_store_command_set_clip_rect_single_data {
        eka2l1::rect clipping_rect_;
    };

    struct gdi_store_command_set_clip_rect_multiple_data {
        eka2l1::rect *rects_;
        std::uint32_t rect_count_;
    };

    static constexpr std::size_t MAX_COMMAND_STORE_DATA_SIZE = sizeof(gdi_store_command_draw_bitmap_data);

    struct gdi_store_command {
        gdi_store_command_opcode opcode_ = gdi_store_command_invalid;
        // Zeroed, so a producer that predates a field (the draw-text flags) leaves it off, not random.
        alignas(std::max_align_t) std::uint8_t data_[MAX_COMMAND_STORE_DATA_SIZE] = {};
        std::shared_ptr<std::vector<std::uint8_t>> dynamic_data_;

        std::uint8_t *allocate_dynamic_data(const std::size_t size) {
            if (!dynamic_data_) {
                dynamic_data_ = std::make_shared<std::vector<std::uint8_t>>();
            }

            dynamic_data_->resize(size);
            return dynamic_data_->data();
        }

        template <typename T>
        T &get_data_struct() {
            static_assert(sizeof(T) <= MAX_COMMAND_STORE_DATA_SIZE);
            static_assert(alignof(T) <= alignof(std::max_align_t));
            return *reinterpret_cast<T*>(data_);
        }
        
        template <typename T>
        const T &get_data_struct_const() const {
            static_assert(sizeof(T) <= MAX_COMMAND_STORE_DATA_SIZE);
            static_assert(alignof(T) <= alignof(std::max_align_t));
            return *reinterpret_cast<const T*>(data_);
        }
    };

    enum gdi_store_command_segment_type {
        gdi_store_command_segment_non_redraw,
        gdi_store_command_segment_pending_redraw,
        gdi_store_command_segment_redraw
    };

    struct gdi_store_command_segment {
        gdi_store_command_segment_type type_;
        std::uint64_t creation_date_;

        common::region region_;
        std::vector<gdi_store_command> commands_;

        // A non-redraw segment past its age or count limit whose window was invalidated so the client
        // redraws it; it is still replayed until that redraw replaces it (see clean_old_nonredraw_segments).
        bool superseded_ = false;

        std::vector<void*> font_objects_;
        std::vector<void*> bitmap_objects_;

        // Driver textures this segment's bitmap commands draw, held in the bitmap cache so that their
        // content stays what was blitted (see bitmap_cache::retain).
        bitmap_cache *texture_owner_ = nullptr;
        std::vector<drivers::handle> held_textures_;

        ~gdi_store_command_segment();

        /**
         * @param cache  When given, the textures a bitmap command already resolved are held in it for as
         *               long as this segment lives.
         */
        void add_command(gdi_store_command &cmd, bitmap_cache *cache = nullptr);
    };

    // The part of a segment's window that its commands paint opaquely (plain-mode opaque fills and
    // unmasked blits of bitmaps without alpha), clipped as the replay clips them.
    common::region gdi_store_segment_opaque_coverage(const gdi_store_command_segment &segment);

    class gdi_store_command_collection {
    private:
        std::vector<std::unique_ptr<gdi_store_command_segment>> segments_;
        gdi_store_command_segment *current_segment_;

    public:
        static constexpr std::uint32_t LIMIT_NON_REDRAW_SEGMENTS = 20;
        static constexpr std::int32_t KEEP_NON_REDRAW_SEGMENTS = 12;
        static constexpr std::uint64_t AGE_LIMIT_NONREDRAW_US = 1000000;
        static constexpr std::size_t LIMIT_SUPERSEDED_SEGMENTS = 64;
        static constexpr std::size_t LIMIT_REDRAW_SEGMENTS = 32;

        explicit gdi_store_command_collection();

        gdi_store_command_segment *add_new_segment(const eka2l1::rect &draw_rect, const gdi_store_command_segment_type type_);
        // Ends a redraw. background_clears: the window has a background colour, so the redraw rectangle
        // was cleared and replaces everything older under it. Without one, only what the redraw paints
        // opaquely replaces older content (gdi_store_segment_opaque_coverage).
        void promote_last_segment(const bool background_clears = true);
        
        /**
         * \brief Retire non-redraw segments past the count or age limit.
         *
         * \param keep_until_redraw  Keep them and only mark them superseded, for a window server without redraw
         *                           storing (Symbian OS 8.1b and older). There the client drew those pixels once,
         *                           outside any redraw, and expects them to stay: erasing the segments erased the
         *                           drawing (a Series 80 menu pane lost the un-highlight of the item it left). They
         *                           are dropped when the client's redraw of the whole window replaces them.
         * \returns True if this must cause an invalidation of the whole window.
         */
        bool clean_old_nonredraw_segments(const bool keep_until_redraw = false);
        void redraw_done();

        gdi_store_command_segment *get_current_segment() const {
            return current_segment_;
        }

        const std::vector<std::unique_ptr<gdi_store_command_segment>> &get_segments() const {
            return segments_;
        }
    };

    // Builds texture update commands (gdi_store_command_update_texture) and submits them to the driver at
    // once, so an upload the bitmap cache has promised never depends on some window being drawn later.
    void gdi_submit_texture_updates(drivers::graphics_driver *driver, bitmap_cache &bcache,
        std::initializer_list<const gdi_store_command *> updates);

    // The rectangle a BitBltMasked with a brush fills before the masked draw: the blitted part of the
    // source (all of it for an empty source rectangle), clipped to the source bitmap.
    eka2l1::rect masked_blit_brush_area(const eka2l1::vec2 &dest_top, const eka2l1::rect &source_rect, const eka2l1::vec2 &bitmap_size);

    class gdi_command_builder {
    private:
        drivers::graphics_driver *driver_;
        drivers::graphics_command_builder &builder_;
        bitmap_cache &bcache_;
        float scale_factor_;
        eka2l1::vec2 position_;
        common::region clip_;
        drivers::filter_option texture_filter_;
        bool premultiplied_target_;
        std::uint32_t draw_mode_ = gdi_draw_mode_pen;

        // Runs draw once per blend pass of the current draw mode, with the pass's colour as brush colour.
        void draw_with_mode(const eka2l1::vec4 &color, const std::function<void()> &draw);
        void build_line_geometry(const gdi_store_command_draw_line_data &cmd);
        void clip_to_region(const common::region &clipped);

    public:
        explicit gdi_command_builder(drivers::graphics_driver *drv, drivers::graphics_command_builder &builder, bitmap_cache &bcache,
            drivers::filter_option texture_filter, const eka2l1::vec2 &position, float scale_factor, const common::region &clip, bool premultiplied_target = false);

        void set_position(const eka2l1::vec2 &pos) {
            position_ = pos;
        }

        void set_clip_region(const common::region &region) {
            clip_ = region;
        }

        void set_scale_factor(const float factor) {
            scale_factor_ = factor;
        }

        void build_segment(const gdi_store_command_segment &segment);
        void build_texture_updates(const gdi_store_command_segment &segment);
        void build_single_command(const gdi_store_command &command);
        void build_command_draw_rect(const gdi_store_command_draw_rect_data &cmd);
        void build_command_draw_line(const gdi_store_command_draw_line_data &cmd);
        void build_command_draw_polygon(const gdi_store_command_draw_polygon_data &cmd);
        void build_command_draw_text(const gdi_store_command_draw_text_data &cmd);
        void build_command_draw_raw_texture(const gdi_store_command_draw_raw_texture_data &cmd);
        void build_command_draw_bitmap(const gdi_store_command_draw_bitmap_data &cmd);
        void build_command_set_clip_rect_single(const gdi_store_command_set_clip_rect_single_data &cmd);
        void build_command_set_clip_rect_multiple(const gdi_store_command_set_clip_rect_multiple_data &cmd);
        void build_command_disable_clip();
        void build_command_update_texture(const gdi_store_command_update_texture_data &cmd);
    };
}
