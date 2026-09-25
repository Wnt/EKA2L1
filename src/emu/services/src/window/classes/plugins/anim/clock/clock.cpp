/*
 * Copyright (c) 2023 EKA2L1 Team
 *
 * This file is part of EKA2L1 project
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

#include <services/window/classes/plugins/anim/clock/clock.h>
#include <services/window/classes/gstore.h>
#include <services/window/classes/winuser.h>
#include <services/window/window.h>

#include <services/fbs/bitmap.h>
#include <services/fbs/fbs.h>

#include <kernel/kernel.h>

#include <common/log.h>
#include <common/rgb.h>
#include <common/time.h>
#include <utils/err.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eka2l1::epoc {
    // KCheckValueForEndOfTimeDeviceConstructionBuf: the last word of every clock constructor buffer.
    static constexpr std::uint32_t CLOCK_CONSTRUCTION_CHECK_VALUE = 0x3af96b5e;

    // CGraphicsContext::ENullPen / ENullBrush
    static constexpr std::int32_t CLOCK_NULL_PEN = 0;
    static constexpr std::int32_t CLOCK_NULL_BRUSH = 0;

    // TDigitalDisplayLayoutChar::EDigitalDisplayLayoutCharFlashingBlockDelimiter
    static constexpr char16_t CLOCK_FLASHING_BLOCK_DELIMITER = 1;

    static constexpr std::int64_t SECONDS_FROM_0AD_TO_1970 = 62168256000LL;

    namespace {
        struct clock_byte_reader {
            const std::uint8_t *cur_;
            const std::uint8_t *end_;
            bool ok_ = true;

            explicit clock_byte_reader(const std::uint8_t *data, const std::size_t size)
                : cur_(data)
                , end_(data + size) {
            }

            std::size_t remaining() const {
                return static_cast<std::size_t>(end_ - cur_);
            }

            std::int32_t i32() {
                std::int32_t value = 0;

                if (!ok_ || (remaining() < sizeof(value))) {
                    ok_ = false;
                    return 0;
                }

                std::memcpy(&value, cur_, sizeof(value));
                cur_ += sizeof(value);

                return value;
            }

            std::uint32_t u32() {
                return static_cast<std::uint32_t>(i32());
            }

            eka2l1::vec2 point() {
                const std::int32_t x = i32();
                const std::int32_t y = i32();

                return eka2l1::vec2(x, y);
            }

            // DAnimWithUtils::ReadText: UTF-16 text, padded to a multiple of four bytes.
            std::u16string text(const std::int32_t length) {
                if (!ok_ || (length < 0) || (length > 0x1000)) {
                    ok_ = false;
                    return std::u16string();
                }

                const std::size_t bytes = static_cast<std::size_t>(length) * sizeof(char16_t);
                const std::size_t padded = (bytes + 3) & ~static_cast<std::size_t>(3);

                if (remaining() < padded) {
                    ok_ = false;
                    return std::u16string();
                }

                std::u16string result(static_cast<std::size_t>(length), u'\0');
                std::memcpy(result.data(), cur_, bytes);

                cur_ += padded;
                return result;
            }
        };

        void read_shadow(clock_byte_reader &reader, clock_shadow &shadow) {
            shadow.on_ = (reader.i32() != 0);
            shadow.color_ = reader.u32();
            shadow.offset_ = reader.point();
        }

        void read_margins(clock_byte_reader &reader, clock_margins &margins) {
            // TMargins: iLeft, iRight, iTop, iBottom
            margins.left_ = reader.i32();
            margins.right_ = reader.i32();
            margins.top_ = reader.i32();
            margins.bottom_ = reader.i32();
        }

        bool read_hand_feature(clock_byte_reader &reader, clock_hand_feature &feature) {
            feature.type_ = static_cast<clock_hand_feature_type>(reader.i32());

            switch (feature.type_) {
            case clock_hand_feature_line:
                // SAnalogDisplayHandLineConstructorArgs
                feature.pen_style_ = reader.i32();
                feature.pen_color_ = reader.u32();
                feature.pen_size_ = reader.point();
                feature.points_.push_back(reader.point());
                feature.points_.push_back(reader.point());
                break;

            case clock_hand_feature_polyline: {
                // SAnalogDisplayHandPolyLineConstructorArgs, then the points
                feature.pen_style_ = reader.i32();
                feature.pen_color_ = reader.u32();
                feature.pen_size_ = reader.point();
                feature.brush_style_ = reader.i32();
                feature.brush_color_ = reader.u32();
                feature.closed_ = (reader.i32() != 0);

                const std::int32_t count = reader.i32();
                if ((count <= 0) || (count > 0x7FFF)) {
                    return false;
                }

                for (std::int32_t i = 0; i < count; i++) {
                    feature.points_.push_back(reader.point());
                }

                break;
            }

            case clock_hand_feature_circle:
                // SAnalogDisplayHandCircleConstructorArgs
                feature.pen_style_ = reader.i32();
                feature.pen_color_ = reader.u32();
                feature.pen_size_ = reader.point();
                feature.brush_style_ = reader.i32();
                feature.brush_color_ = reader.u32();
                feature.points_.push_back(reader.point());
                feature.radius_ = reader.i32();
                break;

            default:
                return false;
            }

            return reader.ok_;
        }

        /**
         * Parse a whole constructor buffer. Series 80 v2's CLOCK.DLL (RAnalogClock::ConstructL with aOffsetHandCenter)
         * appends the hand-centre offset to the analog display arguments; the check value at the end tells the two
         * layouts apart.
         */
        bool parse_clock_buffer_impl(const std::uint8_t *data, const std::size_t size, const bool with_hand_centre_offset, clock_constructor_args &out) {
            clock_byte_reader reader(data, size);

            // SClockConstructorArgs
            out.universal_time_offset_ = reader.i32();
            out.type_ = static_cast<clock_display_type>(reader.i32());

            switch (out.type_) {
            case clock_display_digital: {
                // SDigitalDisplayConstructorArgs
                out.position_ = reader.point();
                out.size_ = reader.point();
                read_margins(reader, out.margins_);
                read_shadow(reader, out.shadow_);
                out.background_color_ = reader.u32();

                const std::int32_t count = reader.i32();
                if (!reader.ok_ || (count <= 0) || (count > 0x7FFF)) {
                    return false;
                }

                for (std::int32_t i = 0; i < count; i++) {
                    // SDigitalDisplayTextSectionConstructorArgs, then the format
                    clock_text_section section;
                    section.font_handle_ = reader.u32();
                    section.color_ = reader.u32();
                    section.horizontal_alignment_ = reader.i32();
                    section.vertical_alignment_ = reader.i32();
                    section.horizontal_margin_ = reader.i32();
                    section.vertical_margin_ = reader.i32();
                    section.format_ = reader.text(reader.i32());

                    if (!reader.ok_) {
                        return false;
                    }

                    out.sections_.push_back(std::move(section));
                }

                break;
            }

            case clock_display_analog: {
                // SAnalogDisplayConstructorArgs. Series 80's has the hand-centre offset between the margins and the
                // shadow: the 9300 Clock sends position, size, margins, (0, 0), shadow on/colour/offset, face, mask...
                out.position_ = reader.point();
                out.size_ = reader.point();
                read_margins(reader, out.margins_);

                if (with_hand_centre_offset) {
                    out.hand_centre_offset_ = reader.point();
                }

                read_shadow(reader, out.shadow_);
                out.face_handle_ = reader.u32();
                out.face_mask_handle_ = reader.u32();

                const std::int32_t count = reader.i32();
                out.has_am_pm_ = (reader.i32() != 0);

                if (!reader.ok_ || (count <= 0) || (count > 0x7FFF)) {
                    return false;
                }

                if (out.has_am_pm_) {
                    // SAnalogDisplayAmPm
                    out.am_pm_position_ = reader.point();
                    out.am_pm_size_ = reader.point();
                    read_shadow(reader, out.am_pm_shadow_);
                    out.am_pm_background_color_ = reader.u32();
                    out.am_pm_font_handle_ = reader.u32();
                    out.am_pm_text_color_ = reader.u32();
                }

                for (std::int32_t i = 0; i < count; i++) {
                    // SAnalogDisplayHandConstructorArgs, then the features
                    clock_hand hand;
                    hand.type_ = static_cast<clock_hand_type>(reader.i32());

                    const std::int32_t feature_count = reader.i32();
                    if (!reader.ok_ || (feature_count <= 0) || (feature_count > 0x7FFF) || (hand.type_ < clock_hand_one_rev_per_12_hours)
                        || (hand.type_ > clock_hand_one_rev_per_minute)) {
                        return false;
                    }

                    for (std::int32_t j = 0; j < feature_count; j++) {
                        clock_hand_feature feature;
                        if (!read_hand_feature(reader, feature)) {
                            return false;
                        }

                        hand.features_.push_back(std::move(feature));
                    }

                    out.hands_.push_back(std::move(hand));
                }

                break;
            }

            default:
                return false;
            }

            return reader.ok_ && (reader.u32() == CLOCK_CONSTRUCTION_CHECK_VALUE) && reader.ok_;
        }

        // TTrig (UTILS.CPP): cosine in Q15 from a table of 15 steps per quadrant, linearly interpolated,
        // so hands land on the same pixels as on the device.
        const std::int32_t CLOCK_TRIG_STORE[] = {
            32768, 32588, 32052, 31164, 29935, 28378, 26510, 24351, 21926, 19261, 16384, 13328, 10126, 6813, 3425, 0, 0
        };

        std::int32_t clock_cos_q15(std::int32_t degrees) {
            while (degrees < 0) {
                degrees += 360;
            }

            degrees %= 360;

            bool negative = false;
            if (degrees > 180) {
                degrees %= 180;
                negative = !negative;
            }

            if (degrees > 90) {
                degrees = 180 - degrees;
                negative = !negative;
            }

            const std::int32_t lower = degrees / 6;
            const std::int32_t interpolation = degrees % 6;

            std::int32_t value = CLOCK_TRIG_STORE[lower];
            if (interpolation > 0) {
                value += (interpolation * (CLOCK_TRIG_STORE[lower + 1] - CLOCK_TRIG_STORE[lower])) / 6;
            }

            return negative ? -value : value;
        }

        std::int32_t clock_sin_q15(const std::int32_t degrees) {
            return clock_cos_q15(degrees - 90);
        }

        // TFraction::operator*
        std::int32_t clock_fraction_mul(const std::int32_t fraction, const std::int32_t value) {
            const std::int32_t temp = fraction * value;
            return (temp < 0) ? -((-temp) >> 15) : (temp >> 15);
        }

        eka2l1::vec2 clock_rotate(const eka2l1::vec2 &point, const std::int32_t sin_q15, const std::int32_t cos_q15, const eka2l1::vec2 &centre) {
            return eka2l1::vec2(clock_fraction_mul(cos_q15, point.x) - clock_fraction_mul(sin_q15, point.y),
                       clock_fraction_mul(cos_q15, point.y) + clock_fraction_mul(sin_q15, point.x))
                + centre;
        }

        /**
         * One primitive's pixels, collected in its bounding box and then emitted as horizontal runs. The window
         * server's command stores have no filled polygon, ellipse or thick line, so the clock's hands are
         * rasterised here, pixel for pixel, the way BITGDI draws them.
         */
        class clock_coverage {
            eka2l1::vec2 origin_;
            std::int32_t width_;
            std::int32_t height_;
            std::vector<std::uint8_t> bits_;

        public:
            explicit clock_coverage(const eka2l1::vec2 &top_left, const eka2l1::vec2 &bottom_right_exclusive)
                : origin_(top_left)
                , width_(std::max<std::int32_t>(0, bottom_right_exclusive.x - top_left.x))
                , height_(std::max<std::int32_t>(0, bottom_right_exclusive.y - top_left.y)) {
                bits_.resize(static_cast<std::size_t>(width_) * height_, 0);
            }

            void set(const std::int32_t x, const std::int32_t y) {
                const std::int32_t lx = x - origin_.x;
                const std::int32_t ly = y - origin_.y;

                if ((lx < 0) || (ly < 0) || (lx >= width_) || (ly >= height_)) {
                    return;
                }

                bits_[static_cast<std::size_t>(ly) * width_ + lx] = 1;
            }

            void emit(gdi_store_command_segment &segment, const common::rgba color) const {
                eka2l1::vec4 colour_vec = common::rgba_to_vec(color);
                colour_vec.w = 255;

                for (std::int32_t y = 0; y < height_; y++) {
                    std::int32_t x = 0;

                    while (x < width_) {
                        if (!bits_[static_cast<std::size_t>(y) * width_ + x]) {
                            x++;
                            continue;
                        }

                        const std::int32_t start = x;
                        while ((x < width_) && bits_[static_cast<std::size_t>(y) * width_ + x]) {
                            x++;
                        }

                        gdi_store_command command;
                        command.opcode_ = gdi_store_command_draw_rect;

                        gdi_store_command_draw_rect_data &data = command.get_data_struct<gdi_store_command_draw_rect_data>();
                        data.color_ = colour_vec;
                        data.rect_ = eka2l1::rect(eka2l1::vec2(origin_.x + start, origin_.y + y), eka2l1::vec2(x - start, 1));

                        segment.add_command(command);
                    }
                }
            }
        };

        // The pixels of a pen of the given size put down at a point: CFbsBitGc's pen is an ellipse filling the
        // rectangle DAnalogDisplayHandFeature::AdjustRectForPenSizeP grows a point by.
        void clock_plot_pen(clock_coverage &coverage, const std::int32_t x, const std::int32_t y, const eka2l1::vec2 &pen_size) {
            const std::int32_t w = std::max<std::int32_t>(pen_size.x, 1);
            const std::int32_t h = std::max<std::int32_t>(pen_size.y, 1);

            const std::int32_t left = x - (w - 1) / 2;
            const std::int32_t top = y - (h - 1) / 2;

            if ((w <= 2) && (h <= 2)) {
                for (std::int32_t py = 0; py < h; py++) {
                    for (std::int32_t px = 0; px < w; px++) {
                        coverage.set(left + px, top + py);
                    }
                }

                return;
            }

            const double rx = w / 2.0;
            const double ry = h / 2.0;

            for (std::int32_t py = 0; py < h; py++) {
                for (std::int32_t px = 0; px < w; px++) {
                    const double dx = (px + 0.5 - rx) / rx;
                    const double dy = (py + 0.5 - ry) / ry;

                    if (dx * dx + dy * dy <= 1.0) {
                        coverage.set(left + px, top + py);
                    }
                }
            }
        }

        void clock_plot_line(clock_coverage &coverage, eka2l1::vec2 from, const eka2l1::vec2 &to, const eka2l1::vec2 &pen_size) {
            const std::int32_t dx = std::abs(to.x - from.x);
            const std::int32_t dy = -std::abs(to.y - from.y);
            const std::int32_t sx = (from.x < to.x) ? 1 : -1;
            const std::int32_t sy = (from.y < to.y) ? 1 : -1;

            std::int32_t error = dx + dy;

            while (true) {
                clock_plot_pen(coverage, from.x, from.y, pen_size);

                if ((from.x == to.x) && (from.y == to.y)) {
                    break;
                }

                const std::int32_t twice = 2 * error;

                if (twice >= dy) {
                    error += dy;
                    from.x += sx;
                }

                if (twice <= dx) {
                    error += dx;
                    from.y += sy;
                }
            }
        }

        // Even-odd fill (CGraphicsContext::EAlternate), sampled at pixel centres.
        void clock_fill_polygon(clock_coverage &coverage, const std::vector<eka2l1::vec2> &points, const std::int32_t top, const std::int32_t bottom) {
            if (points.size() < 3) {
                return;
            }

            std::vector<double> crossings;

            for (std::int32_t y = top; y < bottom; y++) {
                const double sample_y = y + 0.5;
                crossings.clear();

                for (std::size_t i = 0; i < points.size(); i++) {
                    const eka2l1::vec2 &a = points[i];
                    const eka2l1::vec2 &b = points[(i + 1) % points.size()];

                    if ((a.y == b.y) || (sample_y < std::min(a.y, b.y)) || (sample_y >= std::max(a.y, b.y))) {
                        continue;
                    }

                    crossings.push_back(a.x + (sample_y - a.y) * static_cast<double>(b.x - a.x) / static_cast<double>(b.y - a.y));
                }

                std::sort(crossings.begin(), crossings.end());

                for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                    const std::int32_t start = static_cast<std::int32_t>(std::ceil(crossings[i] - 0.5));
                    const std::int32_t end = static_cast<std::int32_t>(std::floor(crossings[i + 1] - 0.5));

                    for (std::int32_t x = start; x <= end; x++) {
                        coverage.set(x, y);
                    }
                }
            }
        }

        // The ellipse filling the rectangle [top_left, bottom_right), sampled at pixel centres.
        void clock_fill_ellipse(clock_coverage &coverage, const eka2l1::vec2 &top_left, const eka2l1::vec2 &bottom_right, const double shrink = 0.0) {
            const double rx = (bottom_right.x - top_left.x) / 2.0 - shrink;
            const double ry = (bottom_right.y - top_left.y) / 2.0 - shrink;

            if ((rx <= 0.0) || (ry <= 0.0)) {
                return;
            }

            const double cx = (top_left.x + bottom_right.x) / 2.0;
            const double cy = (top_left.y + bottom_right.y) / 2.0;

            for (std::int32_t y = top_left.y; y < bottom_right.y; y++) {
                for (std::int32_t x = top_left.x; x < bottom_right.x; x++) {
                    const double dx = (x + 0.5 - cx) / rx;
                    const double dy = (y + 0.5 - cy) / ry;

                    if (dx * dx + dy * dy <= 1.0) {
                        coverage.set(x, y);
                    }
                }
            }
        }

        // The pixels of the ellipse filling [top_left, bottom_right) that are within `thickness` of its edge.
        void clock_ellipse_ring(clock_coverage &coverage, const eka2l1::vec2 &top_left, const eka2l1::vec2 &bottom_right, const double thickness) {
            const double rx = (bottom_right.x - top_left.x) / 2.0;
            const double ry = (bottom_right.y - top_left.y) / 2.0;

            if ((rx <= 0.0) || (ry <= 0.0)) {
                return;
            }

            const double cx = (top_left.x + bottom_right.x) / 2.0;
            const double cy = (top_left.y + bottom_right.y) / 2.0;
            const double irx = rx - thickness;
            const double iry = ry - thickness;

            for (std::int32_t y = top_left.y; y < bottom_right.y; y++) {
                for (std::int32_t x = top_left.x; x < bottom_right.x; x++) {
                    const double ox = (x + 0.5 - cx) / rx;
                    const double oy = (y + 0.5 - cy) / ry;

                    if (ox * ox + oy * oy > 1.0) {
                        continue;
                    }

                    if ((irx > 0.0) && (iry > 0.0)) {
                        const double ix = (x + 0.5 - cx) / irx;
                        const double iy = (y + 0.5 - cy) / iry;

                        if (ix * ix + iy * iy <= 1.0) {
                            continue;
                        }
                    }

                    coverage.set(x, y);
                }
            }
        }

        struct clock_bounds {
            eka2l1::vec2 min_{ 0x7FFFFFFF, 0x7FFFFFFF };
            eka2l1::vec2 max_{ -0x7FFFFFFF, -0x7FFFFFFF };

            void add(const eka2l1::vec2 &point, const eka2l1::vec2 &pen_size) {
                const std::int32_t w = std::max<std::int32_t>(pen_size.x, 1);
                const std::int32_t h = std::max<std::int32_t>(pen_size.y, 1);

                min_.x = std::min(min_.x, point.x - (w - 1) / 2);
                min_.y = std::min(min_.y, point.y - (h - 1) / 2);
                max_.x = std::max(max_.x, point.x + w / 2 + 1);
                max_.y = std::max(max_.y, point.y + h / 2 + 1);
            }
        };

        void clock_draw_feature(gdi_store_command_segment &segment, const clock_hand_feature &feature, const std::int32_t sin_q15,
            const std::int32_t cos_q15, const eka2l1::vec2 &centre, const common::rgba *override_color) {
            std::vector<eka2l1::vec2> points;
            for (const eka2l1::vec2 &point : feature.points_) {
                points.push_back(clock_rotate(point, sin_q15, cos_q15, centre));
            }

            const common::rgba pen_color = override_color ? *override_color : feature.pen_color_;
            const common::rgba brush_color = override_color ? *override_color : feature.brush_color_;

            switch (feature.type_) {
            case clock_hand_feature_line: {
                if ((feature.pen_style_ == CLOCK_NULL_PEN) || (points.size() < 2)) {
                    break;
                }

                clock_bounds bounds;
                bounds.add(points[0], feature.pen_size_);
                bounds.add(points[1], feature.pen_size_);

                clock_coverage coverage(bounds.min_, bounds.max_);
                clock_plot_line(coverage, points[0], points[1], feature.pen_size_);
                coverage.emit(segment, pen_color);

                break;
            }

            case clock_hand_feature_polyline: {
                if (points.empty()) {
                    break;
                }

                clock_bounds bounds;
                for (const eka2l1::vec2 &point : points) {
                    bounds.add(point, feature.pen_size_);
                }

                if (feature.closed_ && (feature.brush_style_ != CLOCK_NULL_BRUSH)) {
                    clock_coverage fill(bounds.min_, bounds.max_);
                    clock_fill_polygon(fill, points, bounds.min_.y, bounds.max_.y);
                    fill.emit(segment, brush_color);
                }

                if (feature.pen_style_ != CLOCK_NULL_PEN) {
                    clock_coverage outline(bounds.min_, bounds.max_);
                    for (std::size_t i = 0; i + 1 < points.size(); i++) {
                        clock_plot_line(outline, points[i], points[i + 1], feature.pen_size_);
                    }

                    if (feature.closed_ && (points.size() > 2)) {
                        clock_plot_line(outline, points.back(), points.front(), feature.pen_size_);
                    }

                    if (points.size() == 1) {
                        clock_plot_pen(outline, points[0].x, points[0].y, feature.pen_size_);
                    }

                    outline.emit(segment, pen_color);
                }

                break;
            }

            case clock_hand_feature_circle: {
                if (points.empty()) {
                    break;
                }

                // DAnalogDisplayHandCircle::Rect
                const eka2l1::vec2 top_left(points[0].x - feature.radius_, points[0].y - feature.radius_);
                const eka2l1::vec2 bottom_right(points[0].x + feature.radius_ + 1, points[0].y + feature.radius_ + 1);

                if (feature.brush_style_ != CLOCK_NULL_BRUSH) {
                    clock_coverage fill(top_left, bottom_right);
                    clock_fill_ellipse(fill, top_left, bottom_right);
                    fill.emit(segment, brush_color);
                }

                if (feature.pen_style_ != CLOCK_NULL_PEN) {
                    // The outline: the pen's thickness inside the ellipse's edge.
                    const double thickness = std::max<std::int32_t>(1, std::max(feature.pen_size_.x, feature.pen_size_.y));

                    clock_coverage ring(top_left, bottom_right);
                    clock_ellipse_ring(ring, top_left, bottom_right, thickness);
                    ring.emit(segment, pen_color);
                }

                break;
            }

            default:
                break;
            }
        }

        void clock_split_time(const std::int64_t local_seconds, std::int32_t &year, std::int32_t &month, std::int32_t &day,
            std::int32_t &hour, std::int32_t &minute, std::int32_t &second, std::int32_t &weekday, std::int32_t &day_of_year) {
            std::int64_t days = local_seconds / 86400;
            std::int64_t rest = local_seconds % 86400;

            if (rest < 0) {
                rest += 86400;
                days--;
            }

            hour = static_cast<std::int32_t>(rest / 3600);
            minute = static_cast<std::int32_t>((rest % 3600) / 60);
            second = static_cast<std::int32_t>(rest % 60);

            // 1970-01-01 was a Thursday; Symbian's TDay starts at EMonday = 0.
            weekday = static_cast<std::int32_t>(((days % 7) + 7 + 3) % 7);

            // Civil date from days since 1970 (Howard Hinnant's algorithm).
            const std::int64_t z = days + 719468;
            const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
            const std::int64_t doe = z - era * 146097;
            const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const std::int64_t mp = (5 * doy + 2) / 153;
            const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
            const std::int64_t m = mp + ((mp < 10) ? 3 : -9);

            year = static_cast<std::int32_t>(yoe + era * 400 + ((m <= 2) ? 1 : 0));
            month = static_cast<std::int32_t>(m);
            day = static_cast<std::int32_t>(d);

            static const std::int32_t CUMULATIVE[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
            const bool leap = ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));

            day_of_year = CUMULATIVE[month - 1] + day + ((leap && (month > 2)) ? 1 : 0);
        }

        std::u16string clock_number(const std::int32_t value, const std::int32_t digits, const bool pad) {
            std::string text = std::to_string(value);

            if (pad) {
                while (static_cast<std::int32_t>(text.length()) < digits) {
                    text.insert(text.begin(), '0');
                }
            }

            return std::u16string(text.begin(), text.end());
        }

        std::u16string clock_ascii(const char *text) {
            const std::string narrow(text);
            return std::u16string(narrow.begin(), narrow.end());
        }
    }

    bool parse_clock_constructor_args(const std::uint8_t *data, const std::size_t size, const bool with_hand_centre_offset,
        clock_constructor_args &out) {
        out = clock_constructor_args();
        return data && parse_clock_buffer_impl(data, size, with_hand_centre_offset, out);
    }

    std::u16string clock_format_time(const std::u16string &format, const std::int64_t local_seconds, const std::uint32_t microseconds,
        const clock_time_locale &locale) {
        static const char *DAY_NAMES[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
        static const char *DAY_ABBREVIATIONS[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
        static const char *MONTH_NAMES[] = { "January", "February", "March", "April", "May", "June", "July", "August",
            "September", "October", "November", "December" };
        static const char *MONTH_ABBREVIATIONS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
            "Nov", "Dec" };

        // English locale defaults (TLocale): 24-hour clock, am/pm after the time with a space, separators
        // "" ":" ":" "" for times and "" "/" "/" "" for dates, European date order.
        static const char16_t TIME_SEPARATORS[] = { 0, u':', u':', 0 };
        static const char16_t DATE_SEPARATORS[] = { 0, u'/', u'/', 0 };

        std::int32_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, weekday = 0, day_of_year = 0;
        clock_split_time(local_seconds, year, month, day, hour, minute, second, weekday, day_of_year);

        std::u16string result;

        for (std::size_t i = 0; i < format.length(); i++) {
            const char16_t c = format[i];

            if (c == CLOCK_FLASHING_BLOCK_DELIMITER) {
                continue;
            }

            if ((c != u'%') || (i + 1 >= format.length())) {
                result += c;
                continue;
            }

            i++;

            bool abbreviate = false;
            bool leading = false;
            bool trailing = false;

            while ((i < format.length()) && ((format[i] == u'*') || (format[i] == u'-') || (format[i] == u'+'))) {
                if (format[i] == u'*') {
                    abbreviate = true;
                } else if (format[i] == u'-') {
                    leading = true;
                } else {
                    trailing = true;
                }

                i++;
            }

            if (i >= format.length()) {
                break;
            }

            const char16_t code = format[i];

            switch (code) {
            case u'%':
                result += u'%';
                break;

            case u':':
            case u'/':
                // A separator, selected by the digit that follows
                if ((i + 1 < format.length()) && (format[i + 1] >= u'0') && (format[i + 1] <= u'3')) {
                    i++;
                    const char16_t separator = (code == u':') ? TIME_SEPARATORS[format[i] - u'0'] : DATE_SEPARATORS[format[i] - u'0'];
                    if (separator) {
                        result += separator;
                    }
                }

                break;

            case u'J':
                if (locale.twelve_hour_) {
                    // The locale's 12-hour clock, without a leading zero: "7:25" beside the am/pm text, as
                    // the 9300's status pane shows it (User Guide p. 21).
                    std::int32_t hour12 = hour % 12;
                    if (hour12 == 0) {
                        hour12 = 12;
                    }

                    result += clock_number(hour12, 2, false);
                    break;
                }

                result += clock_number(hour, 2, !abbreviate);
                break;

            case u'H':
                // 24-hour clock
                result += clock_number(hour, 2, !abbreviate);
                break;

            case u'I': {
                std::int32_t hour12 = hour % 12;
                if (hour12 == 0) {
                    hour12 = 12;
                }

                result += clock_number(hour12, 2, !abbreviate);
                break;
            }

            case u'T':
                result += clock_number(minute, 2, !abbreviate);
                break;

            case u'S':
                result += clock_number(second, 2, !abbreviate);
                break;

            case u'C': {
                std::int32_t digits = 6;
                if (abbreviate && (i + 1 < format.length()) && (format[i + 1] >= u'1') && (format[i + 1] <= u'6')) {
                    i++;
                    digits = format[i] - u'0';
                }

                std::u16string micro = clock_number(static_cast<std::int32_t>(microseconds % 1000000), 6, true);
                result += micro.substr(0, digits);
                break;
            }

            case u'A':
            case u'B': {
                // am/pm text. %B only shows with a 12-hour clock; %A always.
                if ((code == u'B') && !locale.twelve_hour_) {
                    break;
                }

                // Leading (%-A) only when the locale puts it before the time, trailing (%+A) only after.
                if ((leading && !locale.am_pm_before_) || (trailing && locale.am_pm_before_)) {
                    break;
                }

                const std::u16string &text = (hour < 12) ? locale.am_ : locale.pm_;

                if (leading) {
                    result += text;
                    if (locale.am_pm_space_) {
                        result += u' ';
                    }
                } else if (trailing || !abbreviate) {
                    if (locale.am_pm_space_) {
                        result += u' ';
                    }
                    result += text;
                } else {
                    result += text;
                }

                break;
            }

            case u'D':
                result += clock_number(day, 2, !abbreviate);
                break;

            case u'E':
                result += clock_ascii(abbreviate ? DAY_ABBREVIATIONS[weekday] : DAY_NAMES[weekday]);
                break;

            case u'M':
                result += clock_number(month, 2, !abbreviate);
                break;

            case u'N':
                result += clock_ascii(abbreviate ? MONTH_ABBREVIATIONS[month - 1] : MONTH_NAMES[month - 1]);
                break;

            case u'Y':
                result += abbreviate ? clock_number(year % 100, 2, true) : clock_number(year, 4, true);
                break;

            case u'X': {
                const std::int32_t ones = day % 10;
                const std::int32_t tens = (day / 10) % 10;

                if (tens == 1) {
                    result += u"th";
                } else if (ones == 1) {
                    result += u"st";
                } else if (ones == 2) {
                    result += u"nd";
                } else if (ones == 3) {
                    result += u"rd";
                } else {
                    result += u"th";
                }

                break;
            }

            case u'Z':
                result += clock_number(day_of_year, 3, !abbreviate);
                break;

            case u'1':
                result += clock_number(day, 2, !abbreviate);
                break;

            case u'2':
                result += clock_number(month, 2, !abbreviate);
                break;

            case u'3':
                result += abbreviate ? clock_number(year % 100, 2, true) : clock_number(year, 4, true);
                break;

            case u'.':
                result += u'.';
                break;

            case u'F':
                // Fixed date order: ignore the locale's; this one is fixed already.
                break;

            default:
                break;
            }
        }

        return result;
    }

    clock_anim_executor::clock_anim_executor(canvas_base *canvas)
        : anim_executor(canvas)
        , ws_(canvas ? &canvas->client->get_ws() : nullptr)
        , kern_(ws_ ? ws_->get_kernel_system() : nullptr) {
    }

    clock_anim_executor::~clock_anim_executor() {
        const bool was_visible = constructed_ && visible_;

        for (clock_text_section &section : sections_) {
            if (section.font_) {
                section.font_->deref();
            }
        }

        if (am_pm_section_.font_) {
            am_pm_section_.font_->deref();
        }

        if (face_) {
            face_->deref();
        }

        if (face_mask_) {
            face_mask_->deref();
        }

        // DTimeDevice's destructor invalidates what the clock covered.
        if (was_visible) {
            request_redraw(true);
        }
    }

    fbsbitmap *clock_anim_executor::take_bitmap(const std::uint32_t handle) {
        if (!handle || !ws_) {
            return nullptr;
        }

        // MAnimGeneralFunctions::DuplicateBitmapL: the client may free its own handle once the clock is built.
        fbsbitmap *bitmap = ws_->get_raw_fbsbitmap(handle);
        if (bitmap) {
            bitmap->ref();
        }

        return bitmap;
    }

    fbsfont *clock_anim_executor::take_font(const std::uint32_t handle) {
        if (!handle || !ws_) {
            return nullptr;
        }

        fbs_server *fbs = ws_->get_fbs_server();
        fbsfont *font = fbs ? fbs->get_font(handle) : nullptr;

        if (font) {
            font->ref();
        }

        return font;
    }

    common::rgba clock_anim_executor::to_rgba(const std::uint32_t raw) const {
        // TRgb travels as its iValue. EKA1's is 0x00BBGGRR, the layout common::rgba uses; EKA2's is 0xAARRGGBB.
        if (kern_ && !kern_->is_eka1()) {
            return (raw & 0xFF00FF00) | ((raw & 0xFF) << 16) | ((raw & 0xFF0000) >> 16);
        }

        return raw;
    }

    std::int32_t clock_anim_executor::construct(const std::uint8_t *args, const std::size_t args_size) {
        if (!args || !args_size) {
            LOG_ERROR(SERVICE_WINDOW, "Clock animation created without its constructor buffer");
            return epoc::error_argument;
        }

        clock_constructor_args parsed;
        bool parsed_ok = false;

        for (const bool with_hand_centre_offset : { false, true }) {
            parsed = clock_constructor_args();

            if (parse_clock_constructor_args(args, args_size, with_hand_centre_offset, parsed)) {
                parsed_ok = true;
                break;
            }
        }

        if (!parsed_ok) {
            LOG_ERROR(SERVICE_WINDOW, "Unable to parse a clock constructor buffer of {} bytes", args_size);
            return epoc::error_argument;
        }

        universal_time_offset_ = parsed.universal_time_offset_;
        type_ = parsed.type_;
        position_ = parsed.position_;
        size_ = parsed.size_;
        margins_ = parsed.margins_;
        shadow_ = parsed.shadow_;
        shadow_.color_ = to_rgba(shadow_.color_);

        background_color_ = to_rgba(parsed.background_color_);
        sections_ = std::move(parsed.sections_);

        for (clock_text_section &section : sections_) {
            section.color_ = to_rgba(section.color_);
            section.font_ = take_font(section.font_handle_);

            if (!section.font_) {
                LOG_WARN(SERVICE_WINDOW, "Clock text section font handle 0x{:X} does not resolve", section.font_handle_);
            }
        }

        hand_centre_offset_ = parsed.hand_centre_offset_;
        hands_ = std::move(parsed.hands_);

        for (clock_hand &hand : hands_) {
            for (clock_hand_feature &feature : hand.features_) {
                feature.pen_color_ = to_rgba(feature.pen_color_);
                feature.brush_color_ = to_rgba(feature.brush_color_);
            }
        }

        if (type_ == clock_display_analog) {
            face_ = take_bitmap(parsed.face_handle_);
            face_mask_ = take_bitmap(parsed.face_mask_handle_);

            if (!face_) {
                LOG_ERROR(SERVICE_WINDOW, "Analog clock face bitmap handle 0x{:X} does not resolve", parsed.face_handle_);
                return epoc::error_argument;
            }
        }

        has_am_pm_ = parsed.has_am_pm_;
        if (has_am_pm_) {
            am_pm_position_ = parsed.am_pm_position_;
            am_pm_size_ = parsed.am_pm_size_;
            am_pm_shadow_ = parsed.am_pm_shadow_;
            am_pm_shadow_.color_ = to_rgba(am_pm_shadow_.color_);
            am_pm_background_color_ = to_rgba(parsed.am_pm_background_color_);

            // DAnalogDisplay::DAmPmDisplay: one centred section showing "%*A"
            am_pm_section_.font_handle_ = parsed.am_pm_font_handle_;
            am_pm_section_.font_ = take_font(parsed.am_pm_font_handle_);
            am_pm_section_.color_ = to_rgba(parsed.am_pm_text_color_);
            am_pm_section_.horizontal_alignment_ = 1;
            am_pm_section_.vertical_alignment_ = 2;
            am_pm_section_.format_ = u"%*A";
        }

        constructed_ = true;
        state_changed_ = true;

        LOG_TRACE(SERVICE_WINDOW, "Clock animation: {} display at ({}, {}) size {}x{}, universal time offset {} s, {} {}",
            (type_ == clock_display_analog) ? "analog" : "digital", position_.x, position_.y, size_.x, size_.y,
            universal_time_offset_, (type_ == clock_display_analog) ? hands_.size() : sections_.size(),
            (type_ == clock_display_analog) ? "hands" : "text sections");

        return epoc::error_none;
    }

    bool clock_anim_executor::updates_every_second() const {
        if (type_ == clock_display_analog) {
            for (const clock_hand &hand : hands_) {
                if (hand.type_ == clock_hand_one_rev_per_minute) {
                    return true;
                }
            }

            return false;
        }

        // DTimeDevice::ConstructLP: %C and %S tick every second, the flashing block delimiter flashes.
        for (const clock_text_section &section : sections_) {
            std::u16string stripped;
            for (const char16_t c : section.format_) {
                if ((c != u'*') && (c != u'-') && (c != u'+')) {
                    stripped += c;
                }
            }

            for (std::size_t i = 0; i + 1 < stripped.length(); i++) {
                if ((stripped[i] == u'%') && ((stripped[i + 1] == u'S') || (stripped[i + 1] == u's') || (stripped[i + 1] == u'C')
                    || (stripped[i + 1] == u'c'))) {
                    return true;
                }
            }
        }

        return false;
    }

    std::int64_t clock_anim_executor::local_time_seconds() const {
        if (!kern_) {
            return 0;
        }

        const std::int64_t universal_seconds = static_cast<std::int64_t>(kern_->universal_time() / common::microsecs_per_sec);
        return universal_seconds - SECONDS_FROM_0AD_TO_1970 + universal_time_offset_;
    }

    std::int64_t clock_anim_executor::time_key() const {
        const std::int64_t local = local_time_seconds();
        return updates_every_second() ? local : (local / 60);
    }

    bool clock_anim_executor::overlay_dirty() {
        if (!constructed_ || !visible_) {
            return false;
        }

        return state_changed_ || (time_key() != shown_time_key_);
    }

    bool clock_anim_executor::next_overlay_update(std::uint64_t &delay_us) {
        if (!constructed_ || !visible_ || !kern_) {
            return false;
        }

        const std::uint64_t period = (updates_every_second() ? 1 : 60) * common::microsecs_per_sec;
        const std::int64_t local_us = static_cast<std::int64_t>(kern_->universal_time()) + static_cast<std::int64_t>(universal_time_offset_) * common::microsecs_per_sec;

        const std::uint64_t into_period = static_cast<std::uint64_t>(local_us) % period;
        delay_us = period - into_period;

        return true;
    }

    void clock_anim_executor::draw_digital(gdi_store_command_segment &segment, const eka2l1::rect &rect, const common::rgba background,
        const std::vector<clock_text_section> &sections, const clock_shadow &shadow, const std::int64_t local_time) {
        // DDigitalDisplay::DoDrawP: fill with the background colour, then the shadow, then the text.
        {
            gdi_store_command command;
            command.opcode_ = gdi_store_command_draw_rect;

            gdi_store_command_draw_rect_data &data = command.get_data_struct<gdi_store_command_draw_rect_data>();
            data.color_ = common::rgba_to_vec(background);
            data.color_.w = 255;
            data.rect_ = rect;

            segment.add_command(command);
        }

        for (int pass = 0; pass < 2; pass++) {
            const bool shadow_pass = (pass == 0);
            if (shadow_pass && !shadow.on_) {
                continue;
            }

            const eka2l1::vec2 offset = shadow_pass ? shadow.offset_ : eka2l1::vec2(0, 0);

            for (const clock_text_section &section : sections) {
                if (!section.font_) {
                    continue;
                }

                const std::u16string text = clock_format_time(section.format_, local_time);
                if (text.empty()) {
                    continue;
                }

                const std::int32_t ascent = section.font_->of_info.metrics.ascent;
                const std::int32_t descent = section.font_->of_info.metrics.descent;

                // DDigitalDisplayTextSection::YPositionP
                std::int32_t baseline = 0;
                switch (section.vertical_alignment_) {
                case 0:
                    baseline = rect.top.y + section.vertical_margin_ + ascent;
                    break;

                case 1:
                case 2: {
                    const std::int32_t used_descent = (section.vertical_alignment_ == 1) ? descent : 0;
                    baseline = rect.top.y + ascent + ((rect.size.y - (ascent + used_descent)) / 2);
                    break;
                }

                case 3:
                case 4: {
                    const std::int32_t used_descent = (section.vertical_alignment_ == 3) ? descent : 0;
                    baseline = rect.top.y + rect.size.y - section.vertical_margin_ - used_descent;
                    break;
                }

                default:
                    baseline = rect.top.y + ascent;
                    break;
                }

                // DDigitalDisplayTextSection::XPositionP, done by the text box and its alignment.
                eka2l1::rect box;
                epoc::text_alignment alignment = epoc::text_alignment::left;

                switch (section.horizontal_alignment_) {
                case 1:
                    box = eka2l1::rect(eka2l1::vec2(rect.top.x, baseline), eka2l1::vec2(rect.size.x, rect.size.y));
                    alignment = epoc::text_alignment::center;
                    break;

                case 2:
                    box = eka2l1::rect(eka2l1::vec2(rect.top.x, baseline), eka2l1::vec2(rect.size.x - section.horizontal_margin_, rect.size.y));
                    alignment = epoc::text_alignment::right;
                    break;

                default:
                    box = eka2l1::rect(eka2l1::vec2(rect.top.x + section.horizontal_margin_, baseline), eka2l1::vec2(rect.size.x, rect.size.y));
                    break;
                }

                box.top += offset;

                gdi_store_command command;
                command.opcode_ = gdi_store_command_draw_text;

                gdi_store_command_draw_text_data &data = command.get_data_struct<gdi_store_command_draw_text_data>();
                data.string_ = reinterpret_cast<char16_t *>(command.allocate_dynamic_data((text.length() + 1) * sizeof(char16_t)));
                std::memcpy(data.string_, text.c_str(), (text.length() + 1) * sizeof(char16_t));

                data.alignment_ = static_cast<std::uint32_t>(alignment);
                data.text_box_ = box;
                data.fbs_font_ptr_ = section.font_;
                data.color_ = common::rgba_to_vec(shadow_pass ? shadow.color_ : section.color_);
                data.color_.w = 255;

                segment.add_command(command);
            }
        }
    }

    void clock_anim_executor::draw_analog(gdi_store_command_segment &segment, const std::int64_t local_time) {
        const eka2l1::rect rect = rect_drawn_to();

        // DAnalogDisplay::FaceRect: the face bitmap centred in the display rectangle.
        epoc::bitwise_bitmap *face_bitmap = face_->final_clean()->bitmap_;
        const eka2l1::vec2 face_size = face_bitmap->header_.size_pixels;
        const eka2l1::vec2 face_top(rect.top.x + (rect.size.x - face_size.x) / 2, rect.top.y + (rect.size.y - face_size.y) / 2);
        const eka2l1::rect face_rect(face_top, face_size);

        // BitBltMasked(rect.iTl, face, rect relative to the face, mask): only the part of the face inside the rectangle.
        const eka2l1::rect dest = rect.intersect(face_rect);
        if ((dest.size.x > 0) && (dest.size.y > 0)) {
            gdi_store_command command;
            command.opcode_ = gdi_store_command_draw_bitmap;

            gdi_store_command_draw_bitmap_data &data = command.get_data_struct<gdi_store_command_draw_bitmap_data>();
            data.dest_rect_ = dest;
            data.source_rect_ = eka2l1::rect(dest.top - face_top, dest.size);
            data.main_fbs_bitmap_ = face_;
            data.mask_fbs_bitmap_ = face_mask_;
            data.main_drv_ = 0;
            data.mask_drv_ = 0;
            data.gdi_flags_ = 0;

            segment.add_command(command);
        }

        if (has_am_pm_) {
            const eka2l1::rect am_pm_rect(face_top + am_pm_position_, am_pm_size_);
            draw_digital(segment, am_pm_rect, am_pm_background_color_, { am_pm_section_ }, am_pm_shadow_, local_time);
        }

        std::int32_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, weekday = 0, day_of_year = 0;
        clock_split_time(local_time, year, month, day, hour, minute, second, weekday, day_of_year);

        // TRect::Center, then the Series 80 hand-centre offset
        const eka2l1::vec2 centre = eka2l1::vec2(face_top.x + face_size.x / 2, face_top.y + face_size.y / 2) + hand_centre_offset_;

        for (int pass = 0; pass < 2; pass++) {
            const bool shadow_pass = (pass == 0);
            if (shadow_pass && !shadow_.on_) {
                continue;
            }

            const eka2l1::vec2 hand_centre = shadow_pass ? (centre + shadow_.offset_) : centre;

            for (const clock_hand &hand : hands_) {
                // DAnalogDisplayHand::DegreesOffUprightP
                std::int32_t degrees = 0;

                switch (hand.type_) {
                case clock_hand_one_rev_per_12_hours:
                    degrees = (second + 60 * (minute + 60 * hour)) / 120;
                    break;

                case clock_hand_one_rev_per_hour:
                    degrees = (second + 60 * minute) / 10;
                    break;

                case clock_hand_one_rev_per_minute:
                    degrees = 6 * second;
                    break;

                default:
                    break;
                }

                const std::int32_t sin_q15 = clock_sin_q15(degrees);
                const std::int32_t cos_q15 = clock_cos_q15(degrees);

                for (const clock_hand_feature &feature : hand.features_) {
                    clock_draw_feature(segment, feature, sin_q15, cos_q15, hand_centre, shadow_pass ? &shadow_.color_ : nullptr);
                }
            }
        }
    }

    bool clock_anim_executor::build_overlay(gdi_store_command_segment &segment, eka2l1::rect &bounds) {
        if (!constructed_ || !visible_) {
            return false;
        }

        const std::int64_t local_time = local_time_seconds();

        if (type_ == clock_display_analog) {
            if (!face_) {
                return false;
            }

            draw_analog(segment, local_time);
        } else {
            draw_digital(segment, rect_drawn_to(), background_color_, sections_, shadow_, local_time);
        }

        bounds = rect_drawn_to();

        shown_time_key_ = updates_every_second() ? local_time : (local_time / 60);
        state_changed_ = false;

        return true;
    }

    std::int32_t clock_anim_executor::handle_request(service::ipc_context &ctx, const std::int32_t opcode, const std::uint8_t *args,
        const std::size_t args_size) {
        clock_byte_reader reader(args, args ? args_size : 0);

        switch (opcode) {
        case clock_command_set_universal_time_offset: {
            const std::int32_t offset = reader.i32();

            if (reader.ok_ && (offset != universal_time_offset_)) {
                universal_time_offset_ = offset;
                state_changed_ = true;

                request_redraw(false);
            }

            break;
        }

        case display_command_set_visible: {
            const bool visible = (reader.i32() != 0);

            if (reader.ok_ && (visible != visible_)) {
                visible_ = visible;
                state_changed_ = true;

                // Hiding has to bring back the window content under the clock.
                request_redraw(!visible_);
            }

            break;
        }

        case display_command_set_position_and_size:
        case display_command_set_position:
        case display_command_set_size: {
            eka2l1::vec2 position = position_;
            eka2l1::vec2 size = size_;

            if (opcode != display_command_set_size) {
                position = reader.point();
            }

            if (opcode != display_command_set_position) {
                size = reader.point();
            }

            if (reader.ok_ && ((position != position_) || (size != size_))) {
                position_ = position;
                size_ = size;
                state_changed_ = true;

                if (visible_) {
                    request_redraw(true);
                }
            }

            break;
        }

        case display_command_update_display:
        case display_command_draw:
            state_changed_ = true;

            if (visible_) {
                request_redraw(false);
            }

            break;

        case digital_display_command_set_background_color: {
            // SDigitalDisplayCommandSetBackgroundColorArgs. For an analog clock it goes to the am/pm display.
            const common::rgba background = to_rgba(reader.u32());
            const common::rgba shadow = to_rgba(reader.u32());

            if (!reader.ok_) {
                break;
            }

            if (type_ == clock_display_digital) {
                background_color_ = background;
                shadow_.color_ = shadow;
            } else {
                am_pm_background_color_ = background;
                am_pm_shadow_.color_ = shadow;
            }

            state_changed_ = true;
            request_redraw(false);

            break;
        }

        case digital_display_command_set_text_color: {
            const common::rgba color = to_rgba(reader.u32());

            if (!reader.ok_) {
                break;
            }

            for (clock_text_section &section : sections_) {
                section.color_ = color;
            }

            am_pm_section_.color_ = color;
            state_changed_ = true;

            request_redraw(false);
            break;
        }

        case analog_display_command_set_pen_color:
        case analog_display_command_set_brush_color: {
            const common::rgba color = to_rgba(reader.u32());

            if (!reader.ok_) {
                break;
            }

            for (clock_hand &hand : hands_) {
                for (clock_hand_feature &feature : hand.features_) {
                    if (opcode == analog_display_command_set_pen_color) {
                        feature.pen_color_ = color;
                    } else {
                        feature.brush_color_ = color;
                    }
                }
            }

            state_changed_ = true;
            request_redraw(false);

            break;
        }

        case digital_display_command_set_date_time_toggle_s80: {
            // Only remembered: nothing sends ToggleDateTime without a pointer or a key to press on the clock.
            const std::int32_t section = reader.i32();
            if (reader.ok_) {
                date_time_toggle_section_ = section;
            }

            break;
        }

        default: {
            std::string words;
            for (std::size_t i = 0; i + 4 <= args_size; i += 4) {
                std::uint32_t word = 0;
                std::memcpy(&word, args + i, 4);
                words += fmt::format(" 0x{:X}", word);
            }

            LOG_WARN(SERVICE_WINDOW, "Unimplemented clock animation command 0x{:X} ({} bytes of arguments:{})", opcode, args_size, words);
            break;
        }
        }

        return epoc::error_none;
    }
}
