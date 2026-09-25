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

#include <services/window/classes/gstore.h>
#include <drivers/graphics/graphics.h>
#include <services/window/util.h>
#include <services/window/bitmap_cache.h>

#include <services/fbs/font.h>
#include <services/fbs/fbs.h>
#include <services/fbs/bitmap.h>

#include <common/time.h>
#include <common/algorithm.h>

#include <algorithm>
#include <functional>

namespace eka2l1::epoc {
    // NOTE: Must store objects then free ref with local font atlas.
    gdi_store_command_segment::~gdi_store_command_segment() {
        for (std::size_t i = 0; i < font_objects_.size(); i++) {
            reinterpret_cast<fbsfont*>(font_objects_[i])->deref();
        }
        
        for (std::size_t i = 0; i < bitmap_objects_.size(); i++) {
            reinterpret_cast<fbsbitmap*>(bitmap_objects_[i])->deref();
        }

        if (texture_owner_) {
            for (const drivers::handle h : held_textures_) {
                texture_owner_->release(h);
            }
        }
    }

    void gdi_store_command_segment::add_command(gdi_store_command &command, bitmap_cache *cache) {
        if (command.opcode_ == gdi_store_command_draw_text) {
            auto &data = command.get_data_struct_const<gdi_store_command_draw_text_data>();
            if (data.fbs_font_ptr_) {
                auto ite = std::find(font_objects_.begin(), font_objects_.end(), data.fbs_font_ptr_);

                if (ite == font_objects_.end()) {
                    font_objects_.push_back(data.fbs_font_ptr_);
                    reinterpret_cast<fbsfont*>(data.fbs_font_ptr_)->ref();
                }
            }
        }

        if (command.opcode_ == gdi_store_command_draw_bitmap) {
            auto &data = command.get_data_struct_const<gdi_store_command_draw_bitmap_data>();
            if (data.main_fbs_bitmap_ && ((data.gdi_flags_ & GDI_STORE_COMMAND_MAIN_RAW) == 0)) {
                auto ite = std::find(bitmap_objects_.begin(), bitmap_objects_.end(), data.main_fbs_bitmap_);

                if (ite == bitmap_objects_.end()) {
                    bitmap_objects_.push_back(data.main_fbs_bitmap_);
                    reinterpret_cast<fbsbitmap*>(data.main_fbs_bitmap_)->ref();
                }
            }
            
            if (cache) {
                for (const drivers::handle h : { data.main_drv_, data.mask_drv_ }) {
                    if (h && (std::find(held_textures_.begin(), held_textures_.end(), h) == held_textures_.end())) {
                        texture_owner_ = cache;
                        held_textures_.push_back(h);
                        cache->retain(h);
                    }
                }
            }

            if (data.mask_fbs_bitmap_ && ((data.gdi_flags_ & GDI_STORE_COMMAND_MASK_RAW) == 0)) {
                auto ite = std::find(bitmap_objects_.begin(), bitmap_objects_.end(), data.mask_fbs_bitmap_);

                if (ite == bitmap_objects_.end()) {
                    bitmap_objects_.push_back(data.mask_fbs_bitmap_);
                    reinterpret_cast<fbsbitmap*>(data.mask_fbs_bitmap_)->ref();
                }
            }
        }

        commands_.push_back(command);
    }

    gdi_store_command_collection::gdi_store_command_collection()
        : current_segment_(nullptr) {
    }

    gdi_store_command_segment *gdi_store_command_collection::add_new_segment(const eka2l1::rect &draw_rect, const gdi_store_command_segment_type type) {
        std::unique_ptr<gdi_store_command_segment> new_segment = std::make_unique<gdi_store_command_segment>();

        new_segment->type_ = type;
        new_segment->creation_date_ = common::get_current_utc_time_in_microseconds_since_epoch();
        new_segment->region_.add_rect(draw_rect);

        gdi_store_command_segment *new_segment_ptr = new_segment.get();
        segments_.push_back(std::move(new_segment));

        current_segment_ = new_segment_ptr;
        return new_segment_ptr;
    }

    common::region gdi_store_segment_opaque_coverage(const gdi_store_command_segment &segment) {
        common::region coverage;

        // Clipping as the replay applies it: none, one rectangle or a region (window coordinates).
        bool clipped = false;
        common::region clip;
        std::uint32_t draw_mode = gdi_draw_mode_pen;

        const auto cover = [&](const eka2l1::rect &area) {
            if ((area.size.x <= 0) || (area.size.y <= 0)) {
                return;
            }

            if (!clipped) {
                coverage.add_rect(area);
                return;
            }

            common::region piece;
            piece.add_rect(area);
            coverage.add_region(piece.intersect(clip));
        };

        for (const gdi_store_command &command : segment.commands_) {
            switch (command.opcode_) {
            case gdi_store_command_set_clip_rect_single:
                clipped = true;
                clip.make_empty();
                clip.add_rect(command.get_data_struct_const<gdi_store_command_set_clip_rect_single_data>().clipping_rect_);
                break;

            case gdi_store_command_set_clip_rect_multiple: {
                const auto &data = command.get_data_struct_const<gdi_store_command_set_clip_rect_multiple_data>();
                clipped = true;
                clip.make_empty();
                for (std::uint32_t i = 0; i < data.rect_count_; i++) {
                    clip.add_rect(data.rects_[i]);
                }
                break;
            }

            case gdi_store_command_disable_clip:
                clipped = false;
                break;

            case gdi_store_command_set_draw_mode:
                draw_mode = command.get_data_struct_const<gdi_store_command_set_draw_mode_data>().mode_;
                break;

            case gdi_store_command_draw_rect: {
                // Only a fill that replaces the pixels: an opaque colour in a plain mode.
                const auto &data = command.get_data_struct_const<gdi_store_command_draw_rect_data>();
                eka2l1::vec4 color = data.color_;
                gdi_draw_mode_pass passes[2];
                if ((data.color_.w == 255) && (gdi_expand_draw_mode(draw_mode, color, passes) == 0)) {
                    cover(data.rect_);
                }
                break;
            }

            case gdi_store_command_draw_bitmap: {
                // An unmasked blit of a bitmap without alpha replaces its rectangle too.
                const auto &data = command.get_data_struct_const<gdi_store_command_draw_bitmap_data>();
                if (data.mask_fbs_bitmap_ || !data.main_fbs_bitmap_) {
                    break;
                }

                const epoc::bitwise_bitmap *bw = (data.gdi_flags_ & GDI_STORE_COMMAND_MAIN_RAW)
                    ? reinterpret_cast<const epoc::bitwise_bitmap *>(data.main_fbs_bitmap_)
                    : reinterpret_cast<fbsbitmap *>(data.main_fbs_bitmap_)->final_clean()->bitmap_;

                if (!bw || epoc::is_display_mode_alpha(bw->settings_.current_display_mode())) {
                    break;
                }

                eka2l1::rect area = data.dest_rect_;
                const eka2l1::vec2 source_size = ((data.source_rect_.size.x == 0) && (data.source_rect_.size.y == 0))
                    ? eka2l1::vec2(bw->header_.size_pixels) : eka2l1::vec2(data.source_rect_.size);

                if ((area.size.x == 0) && (area.size.y == 0)) {
                    area.size = source_size;
                }

                if (data.gdi_flags_ & GDI_STORE_COMMAND_BLIT) {
                    // A blit never reaches past the bitmap.
                    area.size.x = std::min<int>(area.size.x, bw->header_.size_pixels.x - data.source_rect_.top.x);
                    area.size.y = std::min<int>(area.size.y, bw->header_.size_pixels.y - data.source_rect_.top.y);
                }

                cover(area);
                break;
            }

            default:
                break;
            }
        }

        return coverage;
    }

    void gdi_store_command_collection::promote_last_segment(const bool background_clears) {
        if (segments_.empty()) {
            return;
        }

        gdi_store_command_segment *lastest_segment = segments_.back().get();
        if (lastest_segment->type_ != gdi_store_command_segment_pending_redraw) {
            return;
        }

        if (lastest_segment->region_.empty()) {
            // BeginRedraw on a rectangle that covers no pixel. WSERV opens no segment for it at all
            // (CWsRedrawMsgWindow::DoBeginRedrawL) and drops what is drawn; keeping it would only
            // grow the store by one dead segment per redraw.
            if (current_segment_ == lastest_segment) {
                current_segment_ = nullptr;
            }

            segments_.pop_back();
            return;
        }

        // What the new redraw replaces in the older segments. A window with a background colour is cleared
        // over the whole redraw rectangle first, so everything under it goes. A window without one
        // (RWindow::SetNoBackgroundColor, which most Series 80 controls use) keeps on screen whatever the
        // redraw does not paint over: WSERV 7.0s has no redraw store, the old pixels just stay. Dropping the
        // older segments there left those pixels unpainted (black) at the next recomposition, e.g. the
        // band under the name in a Contacts card, which the pane's later partial redraws never repaint.
        common::region replaced = lastest_segment->region_;
        if (!background_clears) {
            replaced = lastest_segment->region_.intersect(gdi_store_segment_opaque_coverage(*lastest_segment));
        }

        for (std::size_t i = 0; i < replaced.rects_.size(); i++) {
            for (std::size_t j = 0; j < segments_.size(); ) {
                if (segments_[j]->type_ != gdi_store_command_segment_pending_redraw) {
                    segments_[j]->region_.eliminate(replaced.rects_[i]);

                    if (segments_[j]->region_.empty()) {
                        segments_.erase(segments_.begin() + j);
                    } else {
                        j++;
                    }
                } else {
                    j++;
                }
            }
        }

        lastest_segment->type_ = gdi_store_command_segment_redraw;

        // Kept-under segments must not pile up without end when a window keeps redrawing without ever
        // painting some area opaquely: past the limit the oldest go (as they all did before).
        std::size_t redraw_segments = 0;
        for (const auto &segment : segments_) {
            if (segment->type_ == gdi_store_command_segment_redraw) {
                redraw_segments++;
            }
        }

        for (std::size_t j = 0; (redraw_segments > LIMIT_REDRAW_SEGMENTS) && (j < segments_.size()); ) {
            if ((segments_[j]->type_ == gdi_store_command_segment_redraw) && (segments_[j].get() != lastest_segment)
                && (segments_[j].get() != current_segment_)) {
                segments_.erase(segments_.begin() + j);
                redraw_segments--;
            } else {
                j++;
            }
        }
    }

    bool gdi_store_command_collection::clean_old_nonredraw_segments() {
        if (segments_.empty()) {
            return false;
        }

        std::uint32_t non_redraw_segment_count = 0;
        for (std::size_t i = 0; i < segments_.size(); i++) {
            if (segments_[i]->type_ == gdi_store_command_segment_non_redraw) {
                non_redraw_segment_count++;
            }
        }

        // If we managed to clean out some old non-redraw segments, we gotta invalidate all the window
        // Because some non-redraw segment may need to stay there.
        bool need_invalidate = false;
        std::int32_t left_to_keep = KEEP_NON_REDRAW_SEGMENTS;

        if (non_redraw_segment_count > LIMIT_NON_REDRAW_SEGMENTS) {
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(segments_.size()); ) {
                if ((segments_[i].get() != current_segment_) && (segments_[i]->type_ == gdi_store_command_segment_non_redraw)) {
                    if (left_to_keep-- > 0) {
                        i++;
                        continue;
                    } else {
                        segments_.erase(segments_.begin() + i);
                        need_invalidate = true;
                    }
                } else {
                    i++;
                }
            }
        }

        if (current_segment_ && (current_segment_->type_ == gdi_store_command_segment_non_redraw)) {
            std::uint64_t current_time = common::get_current_utc_time_in_microseconds_since_epoch();

            // Try to make it able to be cleaned (this routine is actually from OSS)
            if (current_time - current_segment_->creation_date_ > AGE_LIMIT_NONREDRAW_US) {
                // Try to find older segments
                for (std::size_t i = 0; i < segments_.size(); ) {
                    if ((segments_[i].get() != current_segment_) && (segments_[i]->type_ == gdi_store_command_segment_non_redraw)) {
                        if ((current_time - segments_[i]->creation_date_) > AGE_LIMIT_NONREDRAW_US * 2) {
                            segments_.erase(segments_.begin() + i);
                        } else {
                            i++;
                        }
                    } else {
                        i++;
                    }
                }

                current_segment_->creation_date_ = current_time;
                current_segment_ = nullptr;

                need_invalidate = true;
            }
        }

        return need_invalidate;
    }

    void gdi_store_command_collection::redraw_done() {
        current_segment_ = nullptr;
    }

    std::uint32_t gdi_expand_draw_mode(const std::uint32_t mode, eka2l1::vec4 &color, gdi_draw_mode_pass *passes) {
        const auto invert = [](const eka2l1::vec4 &c) {
            return eka2l1::vec4(255 - c.x, 255 - c.y, 255 - c.z, c.w);
        };

        const std::uint32_t logical_op = mode & gdi_draw_mode_logical_op;
        const bool invert_screen = (mode & gdi_draw_mode_invert_screen) != 0;

        if (mode & gdi_draw_mode_invert_pen) {
            color = invert(color);
        }

        if (logical_op == 0) {
            if (!invert_screen || (mode & gdi_draw_mode_pen)) {
                // PEN, NOTPEN, WriteAlpha: the (possibly inverted) colour replaces the pixel.
                return 0;
            }

            // NOTSCREEN: every covered pixel becomes its inverse, whatever the colour.
            passes[0] = { eka2l1::vec4(255, 255, 255, 255), drivers::blend_factor::one_minus_current_color, drivers::blend_factor::zero };
            return 1;
        }

        const eka2l1::vec4 opaque(color.x, color.y, color.z, 255);

        if (logical_op & gdi_draw_mode_xor) {
            // S ^ D, and ~D ^ S == ~S ^ D.
            passes[0] = { invert_screen ? invert(opaque) : opaque, drivers::blend_factor::one_minus_current_color,
                drivers::blend_factor::one_minus_frag_out_color };
            return 1;
        }

        if (logical_op & gdi_draw_mode_and) {
            // S & D, or S & ~D.
            passes[0] = { opaque, invert_screen ? drivers::blend_factor::one_minus_current_color : drivers::blend_factor::current_color,
                drivers::blend_factor::zero };
            return 1;
        }

        // OR
        if (!invert_screen) {
            // S | D = S * (1 - D) + D
            passes[0] = { opaque, drivers::blend_factor::one_minus_current_color, drivers::blend_factor::one };
            return 1;
        }

        // S | ~D = ~(~S & D): AND with the inverted colour, then invert the screen.
        passes[0] = { invert(opaque), drivers::blend_factor::current_color, drivers::blend_factor::zero };
        passes[1] = { eka2l1::vec4(255, 255, 255, 255), drivers::blend_factor::one_minus_current_color, drivers::blend_factor::zero };
        return 2;
    }

    void gdi_submit_texture_updates(drivers::graphics_driver *driver, bitmap_cache &bcache,
        std::initializer_list<const gdi_store_command *> updates) {
        if (!driver) {
            return;
        }

        drivers::graphics_command_builder builder;
        gdi_command_builder gdi_builder(driver, builder, bcache, drivers::filter_option::linear, eka2l1::vec2(0, 0), 1.0f,
            common::region{});

        bool any = false;
        for (const gdi_store_command *update : updates) {
            if (update && (update->opcode_ == gdi_store_command_update_texture)) {
                gdi_builder.build_single_command(*update);
                any = true;
            }
        }

        if (any) {
            drivers::command_list list = builder.retrieve_command_list();
            driver->submit_command_list(list);
        }
    }

    eka2l1::rect masked_blit_brush_area(const eka2l1::vec2 &dest_top, const eka2l1::rect &source_rect, const eka2l1::vec2 &bitmap_size) {
        eka2l1::rect area(dest_top, source_rect.size);
        if ((area.size.x == 0) && (area.size.y == 0)) {
            area.size = bitmap_size;
        }

        // The blit never reaches past the source bitmap.
        area.size.x = std::max<int>(0, std::min<int>(area.size.x, bitmap_size.x - source_rect.top.x));
        area.size.y = std::max<int>(0, std::min<int>(area.size.y, bitmap_size.y - source_rect.top.y));
        return area;
    }

    gdi_command_builder::gdi_command_builder(drivers::graphics_driver *drv, drivers::graphics_command_builder &builder, bitmap_cache &bcache,
        drivers::filter_option texture_filter, const eka2l1::vec2 &position, float scale_factor, const common::region &clip, bool premultiplied_target)
        : driver_(drv)
        , builder_(builder)
        , bcache_(bcache)
        , scale_factor_(scale_factor)
        , position_(position)
        , clip_(clip)
        , texture_filter_(texture_filter)
        , premultiplied_target_(premultiplied_target) {
    }

    void gdi_command_builder::build_segment(const gdi_store_command_segment &segment) {
        // A segment's recording starts in PEN mode; the graphics context brackets every moded draw.
        draw_mode_ = gdi_draw_mode_pen;

        for (std::size_t i = 0; i < segment.commands_.size(); i++) {
            build_single_command(segment.commands_[i]);
        }
    }

    void gdi_command_builder::build_texture_updates(const gdi_store_command_segment &segment) {
        for (const auto &command : segment.commands_) {
            if (command.opcode_ == gdi_store_command_update_texture) {
                build_command_update_texture(command.get_data_struct_const<gdi_store_command_update_texture_data>());
            }
        }
    }

    void gdi_command_builder::build_single_command(const gdi_store_command &command) {
        // Retained UI stores premultiplied RGBA even for replacement-mode GDI writes.
        if (premultiplied_target_) {
            builder_.set_feature(drivers::graphics_feature::blend, true);
            builder_.blend_formula(drivers::blend_equation::add, drivers::blend_equation::add,
                drivers::blend_factor::frag_out_alpha, drivers::blend_factor::zero,
                drivers::blend_factor::one, drivers::blend_factor::zero);
        }
        switch (command.opcode_) {
        case gdi_store_command_draw_rect:
            build_command_draw_rect(command.get_data_struct_const<gdi_store_command_draw_rect_data>());
            break;

        case gdi_store_command_draw_line:
            build_command_draw_line(command.get_data_struct_const<gdi_store_command_draw_line_data>());
            break;

        case gdi_store_command_draw_polygon:
            build_command_draw_polygon(command.get_data_struct_const<gdi_store_command_draw_polygon_data>());
            break;

        case gdi_store_command_draw_text:
            build_command_draw_text(command.get_data_struct_const<gdi_store_command_draw_text_data>());
            break;

        case gdi_store_command_draw_bitmap:
            build_command_draw_bitmap(command.get_data_struct_const<gdi_store_command_draw_bitmap_data>());
            break;

        case gdi_store_command_disable_clip:
            build_command_disable_clip();
            break;

        case gdi_store_command_set_clip_rect_single:
            build_command_set_clip_rect_single(command.get_data_struct_const<gdi_store_command_set_clip_rect_single_data>());
            break;

        case gdi_store_command_set_clip_rect_multiple:
            build_command_set_clip_rect_multiple(command.get_data_struct_const<gdi_store_command_set_clip_rect_multiple_data>());
            break;

        case gdi_store_command_update_texture:
            build_command_update_texture(command.get_data_struct_const<gdi_store_command_update_texture_data>());
            break;

        case gdi_store_command_set_draw_mode:
            draw_mode_ = command.get_data_struct_const<gdi_store_command_set_draw_mode_data>().mode_;
            break;

        default:
            break;
        }
    }

    void gdi_command_builder::build_command_draw_rect(const gdi_store_command_draw_rect_data &cmd) {
        eka2l1::rect scaled_rect = cmd.rect_;
        scaled_rect.top += position_;

        scale_rectangle(scaled_rect, scale_factor_);

        draw_with_mode(cmd.color_, [&]() {
            builder_.draw_rectangle(scaled_rect);
        });
    }

    void gdi_command_builder::draw_with_mode(const eka2l1::vec4 &color, const std::function<void()> &draw) {
        eka2l1::vec4 plain_color = color;
        gdi_draw_mode_pass passes[2];
        const std::uint32_t pass_count = gdi_expand_draw_mode(draw_mode_, plain_color, passes);

        if (pass_count == 0) {
            builder_.set_brush_color_detail(plain_color);
            draw();
            return;
        }

        builder_.set_feature(drivers::graphics_feature::blend, true);

        for (std::uint32_t i = 0; i < pass_count; i++) {
            // The target's alpha (coverage of a retained window) is left as it is.
            builder_.blend_formula(drivers::blend_equation::add, drivers::blend_equation::add,
                passes[i].src_factor_, passes[i].dst_factor_, drivers::blend_factor::zero, drivers::blend_factor::one);
            builder_.set_brush_color_detail(passes[i].color_);
            draw();
        }

        // Back to what build_single_command set up for plain drawing.
        if (premultiplied_target_) {
            builder_.blend_formula(drivers::blend_equation::add, drivers::blend_equation::add,
                drivers::blend_factor::frag_out_alpha, drivers::blend_factor::zero,
                drivers::blend_factor::one, drivers::blend_factor::zero);
        } else {
            builder_.set_feature(drivers::graphics_feature::blend, false);
        }
    }

    void gdi_command_builder::build_command_draw_line(const gdi_store_command_draw_line_data &cmd) {
        draw_with_mode(cmd.color_, [&]() {
            build_line_geometry(cmd);
        });
    }

    void gdi_command_builder::build_line_geometry(const gdi_store_command_draw_line_data &cmd) {
        eka2l1::point scaled_start = (cmd.start_ + position_) * scale_factor_;
        eka2l1::point scaled_end = (cmd.end_ + position_) * scale_factor_;


        // Trying to emulate brush size here. There's more complexity in adding a real variant.
        if (cmd.style_ == drivers::pen_style_solid) {
            if (((scale_factor_ != 1.0f) || ((cmd.pen_size_.x != 1) || (cmd.pen_size_.y != 1))) && ((cmd.start_.x == cmd.end_.x)
                || (cmd.start_.y == cmd.end_.y))) {
                eka2l1::rect draw_rect;
                draw_rect.top = scaled_start - (cmd.pen_size_ * (scale_factor_ / 2.0f));
                if (cmd.start_.x == cmd.end_.x) {
                    if (cmd.start_.y == cmd.end_.y) {
                        draw_rect.size = cmd.pen_size_ * scale_factor_;
                    } else {
                        draw_rect.size = eka2l1::vec2(static_cast<int>(std::roundf(cmd.pen_size_.x * scale_factor_)), scaled_end.y - scaled_start.y + static_cast<int>(std::roundf(cmd.pen_size_.y * scale_factor_ / 2.0f)));
                    }
                } else {
                    draw_rect.size = eka2l1::vec2(scaled_end.x - scaled_start.x + static_cast<int>(std::roundf(cmd.pen_size_.x * scale_factor_ / 2.0f)), static_cast<int>(std::roundf(cmd.pen_size_.y * scale_factor_)));
                }

                builder_.draw_rectangle(draw_rect);
                return;
            }
        }

        builder_.set_pen_style(cmd.style_);
        builder_.draw_line(scaled_start, scaled_end);
    }

    void gdi_command_builder::build_command_draw_polygon(const gdi_store_command_draw_polygon_data &cmd) {
        std::vector<eka2l1::point> copied_points(cmd.point_count_);
        for (std::size_t i = 0; i < cmd.point_count_; i++) {
            copied_points[i] = (cmd.points_[i] + position_) * scale_factor_;
        }

        draw_with_mode(cmd.color_, [&]() {
            builder_.set_pen_style(cmd.style_);
            builder_.draw_polygons(copied_points.data(), cmd.point_count_);
        });
    }

    void gdi_command_builder::build_command_draw_text(const gdi_store_command_draw_text_data &cmd) {
        builder_.set_brush_color_detail(cmd.color_);
        fbsfont *text_font = reinterpret_cast<fbsfont*>(cmd.fbs_font_ptr_);
        
        std::int16_t scaled_font_size = epoc::font_height_in_pixels(text_font->of_info.metrics, text_font->of_info.adapter->vectorizable());
        std::uint32_t metric_identifier = text_font->of_info.metric_identifier;
        float scale_to_pass = 1.0f;

        if (text_font->of_info.adapter->vectorizable()) {
            scaled_font_size = static_cast<std::int16_t>(scaled_font_size * scale_factor_);
            metric_identifier = scaled_font_size;       // Vectorizable font metric identifier is font size

            if ((text_font->atlas.atlas_handle_ != 0) && (scaled_font_size != text_font->atlas.get_char_size())) {
                text_font->atlas.destroy(driver_);
            }
        } else {
            scale_to_pass = scale_factor_;
        }

        if (text_font->atlas.atlas_handle_ == 0) {
            text_font->atlas.init(text_font->of_info.adapter, text_font->of_info.idx, 0x20, 0xFF - 0x20,
                scaled_font_size, metric_identifier);
        }

        eka2l1::rect scaled_text_box = cmd.text_box_;
        scaled_text_box.top += position_;

        scale_rectangle(scaled_text_box, scale_factor_);

        eka2l1::vec2 pen_span{ 0, 0 };
        text_font->atlas.draw_text(cmd.string_, scaled_text_box, static_cast<epoc::text_alignment>(cmd.alignment_),
            driver_, builder_, { scale_to_pass, scale_to_pass }, premultiplied_target_, &pen_span);

        if ((cmd.text_flags_ & (GDI_STORE_COMMAND_TEXT_UNDERLINE | GDI_STORE_COMMAND_TEXT_STRIKETHROUGH)) && (pen_span.y > pen_span.x)) {
            // CFbsBitGc: both lines are Max(HeightInPixels / 10, 1) thick and span the text's advance; the
            // underline starts 1 + thickness / 2 below the baseline (APIExGetUnderlineMetrics), the
            // strikethrough AscentInPixels * 5 / 12 + 1 above it (GetStrikethroughMetrics). The text box's
            // top is the baseline here.
            const int thickness = std::max<int>(epoc::font_height_in_pixels(text_font->of_info.metrics, text_font->of_info.adapter->vectorizable()) / 10, 1);
            const int baseline = scaled_text_box.top.y;

            builder_.set_brush_color_detail(cmd.color_);

            if (cmd.text_flags_ & GDI_STORE_COMMAND_TEXT_UNDERLINE) {
                const int top = 1 + thickness / 2;
                builder_.draw_rectangle(eka2l1::rect({ pen_span.x, baseline + static_cast<int>(top * scale_factor_) },
                    { pen_span.y - pen_span.x, std::max<int>(static_cast<int>(thickness * scale_factor_), 1) }));
            }

            if (cmd.text_flags_ & GDI_STORE_COMMAND_TEXT_STRIKETHROUGH) {
                const int top = -(text_font->of_info.metrics.ascent * 5 / 12) - 1;
                builder_.draw_rectangle(eka2l1::rect({ pen_span.x, baseline + static_cast<int>(top * scale_factor_) },
                    { pen_span.y - pen_span.x, std::max<int>(static_cast<int>(thickness * scale_factor_), 1) }));
            }
        }
    }

    void gdi_command_builder::build_command_draw_raw_texture(const gdi_store_command_draw_raw_texture_data &cmd) {
        eka2l1::rect scaled_dest_rect = cmd.dest_rect_;
        scaled_dest_rect.top += position_;

        scale_rectangle(scaled_dest_rect, scale_factor_);

        builder_.draw_bitmap(cmd.texture_, 0, scaled_dest_rect, eka2l1::rect{}, eka2l1::vec2(0, 0), 0.0f,
            cmd.flags_);
    }

    void gdi_command_builder::build_command_draw_bitmap(const gdi_store_command_draw_bitmap_data &cmd) {
        epoc::bitwise_bitmap *source_bitmap_bw = reinterpret_cast<epoc::bitwise_bitmap*>(cmd.main_fbs_bitmap_);

        if ((cmd.gdi_flags_ & GDI_STORE_COMMAND_MAIN_RAW) == 0) {
            source_bitmap_bw = reinterpret_cast<fbsbitmap*>(cmd.main_fbs_bitmap_)->final_clean()->bitmap_;
        }

        epoc::bitwise_bitmap *mask_bitmap_bw = reinterpret_cast<epoc::bitwise_bitmap*>(cmd.mask_fbs_bitmap_);

        if (mask_bitmap_bw) {
            if ((cmd.gdi_flags_ & GDI_STORE_COMMAND_MASK_RAW) == 0) {
                mask_bitmap_bw = reinterpret_cast<fbsbitmap*>(cmd.mask_fbs_bitmap_)->final_clean()->bitmap_;
            }
        }

        drivers::handle source_bitmap_drv = cmd.main_drv_;
        if (!source_bitmap_drv) {
            source_bitmap_drv = bcache_.add_or_get(driver_, source_bitmap_bw, &builder_);
        }

        if (!source_bitmap_drv) {
            // The cache refused the bitmap (its pixels are not readable); drawing texture 0 would take
            // the renderer down.
            return;
        }

        drivers::handle mask_bitmap_drv = cmd.mask_drv_;

        if (!mask_bitmap_drv && mask_bitmap_bw) {
            mask_bitmap_drv = bcache_.add_or_get(driver_, mask_bitmap_bw, &builder_);
        }

        if (mask_bitmap_bw && !mask_bitmap_drv) {
            // Same for a refused mask: drawn without it the source's key colour shows, and the
            // mask swizzle below would be sent to texture 0, which takes the renderer down too.
            return;
        }

        eka2l1::rect scaled_dest_rect = cmd.dest_rect_;
        scaled_dest_rect.top += position_;

        eka2l1::rect adjusted_source_rect = cmd.source_rect_;

        if (scaled_dest_rect.size.x == 0) {
            if (adjusted_source_rect.size.x == 0) {
                scaled_dest_rect.size.x = source_bitmap_bw->header_.size_pixels.x;
            } else {
                scaled_dest_rect.size.x = adjusted_source_rect.size.x;
            }
        }

        if (scaled_dest_rect.size.y == 0) {
            if (adjusted_source_rect.size.y == 0) {
                scaled_dest_rect.size.y = source_bitmap_bw->header_.size_pixels.y;
            } else {
                scaled_dest_rect.size.y = adjusted_source_rect.size.y;
            }
        }
        
        scale_rectangle(scaled_dest_rect, scale_factor_);

        // Handle this variant: BitBlt(const TPoint &aDestination, const CFbsBitmap *aBitmap, const TRect &aSource);
        if ((cmd.gdi_flags_ & GDI_STORE_COMMAND_BLIT) &&
            ((cmd.source_rect_.size.y > source_bitmap_bw->header_.size_pixels.y) ||
            (cmd.source_rect_.size.x > source_bitmap_bw->header_.size_pixels.x))) {
            if (!mask_bitmap_drv) {
                builder_.set_brush_color_detail(eka2l1::vec4(255, 255, 255, 255));
                builder_.draw_rectangle(scaled_dest_rect);
            }

            if (cmd.source_rect_.size.y > source_bitmap_bw->header_.size_pixels.y) {
                adjusted_source_rect.size.y = source_bitmap_bw->header_.size_pixels.y;
            }
            
            if (cmd.source_rect_.size.x > source_bitmap_bw->header_.size_pixels.x) {
                adjusted_source_rect.size.x = source_bitmap_bw->header_.size_pixels.x;
            }

            scaled_dest_rect.size = adjusted_source_rect.size * scale_factor_;
        }

        bool swizzle_alteration = false;

        const bool alpha_blending = mask_bitmap_bw && ((mask_bitmap_bw->settings_.current_display_mode() == epoc::display_mode::gray256)
            || (epoc::is_display_mode_alpha(mask_bitmap_bw->settings_.current_display_mode())));

        std::uint32_t flags = 0;

        if ((cmd.gdi_flags_ & GDI_STORE_COMMAND_INVERT_MASK) && !alpha_blending) {
            flags |= drivers::bitmap_draw_flag_invert_mask;
        }

        if (!alpha_blending) {
            flags |= drivers::bitmap_draw_flag_flat_blending;
        }

        if (mask_bitmap_bw) {
            builder_.set_feature(drivers::graphics_feature::blend, true);

            // For non alpha blending we always want to take color buffer's alpha.
            builder_.blend_formula(drivers::blend_equation::add, drivers::blend_equation::add,
                drivers::blend_factor::frag_out_alpha, drivers::blend_factor::one_minus_frag_out_alpha,
                (alpha_blending ? drivers::blend_factor::one : drivers::blend_factor::one),
                ((alpha_blending || premultiplied_target_) ? drivers::blend_factor::one_minus_frag_out_alpha : drivers::blend_factor::one));
        } else if (premultiplied_target_) {
            auto mode = source_bitmap_bw->settings_.current_display_mode();
            if (mode == epoc::display_mode::none) {
                mode = source_bitmap_bw->settings_.initial_display_mode();
            }
            if (mode == epoc::display_mode::color16map) {
                builder_.set_feature(drivers::graphics_feature::blend, false);
            }
        }

        // The mask shader reads the mask's red channel. EColor4K is uploaded as RGBA4444 holding 0RGB,
        // which the driver swizzles (G, B, A, 1) back into place: red lives in the texture's green
        // channel, and the texture's own red channel is the unused top nibble, zero in every pixel.
        // Read through (R, G, B, R) such a mask is empty, and inverted it lets the whole source
        // through, key colour included: the magenta boxes around Series 80 icons with EColor4K masks.
        const bool mask_is_color4k = mask_bitmap_bw && (get_suitable_bpp_for_bitmap(mask_bitmap_bw) == 12);

        if (mask_bitmap_bw && !alpha_blending && !epoc::is_display_mode_alpha(mask_bitmap_bw->settings_.current_display_mode())) {
            swizzle_alteration = true;

            if (mask_is_color4k) {
                builder_.set_swizzle(mask_bitmap_drv, drivers::channel_swizzle::green, drivers::channel_swizzle::blue,
                    drivers::channel_swizzle::alpha, drivers::channel_swizzle::green);
            } else {
                builder_.set_swizzle(mask_bitmap_drv, drivers::channel_swizzle::red, drivers::channel_swizzle::green,
                    drivers::channel_swizzle::blue, drivers::channel_swizzle::red);
            }
        }

        builder_.set_texture_filter(source_bitmap_drv, false, texture_filter_);
        builder_.set_texture_filter(source_bitmap_drv, true, texture_filter_);

        if (cmd.gdi_flags_ & GDI_STORE_COMMAND_TILE) {
            // Patterned brush: the source rectangle runs past the bitmap, the sampler wraps it.
            builder_.set_texture_addressing_mode(source_bitmap_drv, drivers::addressing_direction::s, drivers::addressing_option::repeat);
            builder_.set_texture_addressing_mode(source_bitmap_drv, drivers::addressing_direction::t, drivers::addressing_option::repeat);
        }

        if (mask_bitmap_drv) {
            builder_.set_texture_filter(mask_bitmap_drv, false, texture_filter_);
            builder_.set_texture_filter(mask_bitmap_drv, true, texture_filter_);
        }

        builder_.draw_bitmap(source_bitmap_drv, mask_bitmap_drv, scaled_dest_rect, adjusted_source_rect,
            eka2l1::vec2(0, 0), 0.0f, flags);

        if (mask_bitmap_bw) {
            builder_.set_feature(drivers::graphics_feature::blend, false);
        }
        
        if (swizzle_alteration) {
            // Back to the texture's own channel order, so it still draws right as a plain bitmap.
            if (mask_is_color4k) {
                builder_.set_swizzle(mask_bitmap_drv, drivers::channel_swizzle::green, drivers::channel_swizzle::blue,
                    drivers::channel_swizzle::alpha, drivers::channel_swizzle::one);
            } else {
                builder_.set_swizzle(mask_bitmap_drv, drivers::channel_swizzle::red, drivers::channel_swizzle::green,
                    drivers::channel_swizzle::blue, drivers::channel_swizzle::alpha);
            }
        }
    }

    void gdi_command_builder::build_command_set_clip_rect_single(const gdi_store_command_set_clip_rect_single_data &cmd) {
        eka2l1::rect rect_advanced = cmd.clipping_rect_;
        rect_advanced.top += position_;

        common::region clipped;
        clipped.add_rect(rect_advanced);
        clipped = clipped.intersect(clip_);

        if ((clipped.rects_.size() == 1) && (clipped.rects_[0].size == eka2l1::vec2(1, 1))) {
            LOG_TRACE(KERNEL, "HI!");
        }
        
        builder_.clip_bitmap_region(clipped, scale_factor_);
    }

    void gdi_command_builder::build_command_set_clip_rect_multiple(const gdi_store_command_set_clip_rect_multiple_data &cmd) {
        common::region clipped;
        clipped.rects_.insert(clipped.rects_.begin(), cmd.rects_, cmd.rects_ + cmd.rect_count_);
        clipped.advance(position_);
        clipped = clipped.intersect(clip_);

        builder_.clip_bitmap_region(clipped, scale_factor_);
    }

    void gdi_command_builder::build_command_disable_clip() {
        if (clip_.empty()) {
            builder_.set_feature(drivers::graphics_feature::stencil_test, false);
            builder_.set_feature(drivers::graphics_feature::clipping, false);
        } else {
            builder_.clip_bitmap_region(clip_, scale_factor_);
        }
    }

    void gdi_command_builder::build_command_update_texture(const gdi_store_command_update_texture_data &cmd) {
        if (cmd.destroy_handle_) {
            builder_.destroy_bitmap(cmd.destroy_handle_);
        }

        builder_.update_bitmap(cmd.handle_, reinterpret_cast<const char*>(cmd.texture_data_), cmd.texture_size_,
            eka2l1::vec2(0, 0), cmd.dim_, cmd.pixel_per_line_, false);

        if (cmd.do_swizz_) {
            builder_.set_swizzle(cmd.handle_, cmd.swizz_[0], cmd.swizz_[1], cmd.swizz_[2], cmd.swizz_[3]);
        }
    }
}
