/*
 * Copyright (c) 2019 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 * 
 * Initial contributor: pent0
 * Contributors:
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

#include <common/rgb.h>
#include <common/vecx.h>

#include <drivers/graphics/common.h>
#include <drivers/itc.h>
#include <services/fbs/bitmap.h>

#include <array>
#include <unordered_map>
#include <unordered_set>

namespace eka2l1 {
    class kernel_system;
    class fbs_server;
}

namespace eka2l1::epoc {
    constexpr std::uint32_t MAX_CACHE_SIZE = 1024;
    struct gdi_store_command;

    /**
     * @brief   Bits per pixel of the driver texture the cache uploads for a bitmap.
     *
     * Palette bitmaps and every depth below 8bpp (EGray2, EGray4, EGray16, EColor16) are expanded
     * to 24bpp and extended bitmaps to 32bpp;
     * every other bitmap keeps its own depth. A 12bpp texture holds EColor4K's 0RGB pixels
     * as RGBA4444, which the driver swizzles (G, B, A, 1) back into RGB order.
     */
    std::uint32_t get_suitable_bpp_for_bitmap(epoc::bitwise_bitmap *bmp);

    /**
     * @brief   Bytes in one scan line of a bitmap below 8bpp: padded to whole 32-bit words.
     */
    std::uint32_t sub_byte_bitmap_scanline_bytes(const std::int32_t width, const std::uint32_t bpp);

    /**
     * @brief   Expand a 1, 2 or 4bpp bitmap to the 24bpp (B, G, R) texture layout the cache uploads.
     *
     * Pixels are packed from the least significant bit of each byte. Without a palette each value is a
     * grey level (0 black, 2^bpp - 1 white); with one it indexes 0x00BBGGRR entries (EColor16).
     *
     * @param   stride     Source bytes per scan line; 0 takes the 32-bit padded length.
     * @param   raw_size   Receives the size of the returned buffer.
     *
     * @returns A new[] buffer of rows padded to 4 bytes, or nullptr for another depth.
     */
    char *expand_sub_byte_bitmap_to_24bpp(const std::uint8_t *data, const eka2l1::object_size &size, const std::uint32_t bpp,
        std::uint32_t stride, const common::rgba *palette, std::size_t &raw_size);

    class bitmap_cache {
    public:
        using driver_texture_handle_array = std::array<drivers::handle, MAX_CACHE_SIZE>;
        using bitmap_array = std::array<epoc::bitwise_bitmap *, MAX_CACHE_SIZE>;
        using timestamps_array = std::array<std::uint64_t, MAX_CACHE_SIZE>;
        using hashes_array = timestamps_array;
        using sizes_array = std::array<std::pair<std::uint64_t, std::uint32_t>, MAX_CACHE_SIZE>;

    private:
        driver_texture_handle_array driver_textures;
        bitmap_array bitmaps;
        timestamps_array timestamps;
        hashes_array hashes;
        sizes_array bitmap_sizes;

        fbs_server *fbss_;

        kernel_system *kern;
        drivers::graphics_driver *driver;

        std::int64_t last_free{ 0 };

        // Textures the redraw store still draws. The store keeps a window's drawing and replays it on
        // every recomposition, but a bitmap is a live object: an application's off-screen bitmap is drawn
        // into again and blitted elsewhere (S80 list boxes and dialogs share one to paint a highlighted
        // row). WSERV copied the pixels at blit time; the store must replay what was blitted then, so a
        // texture a stored command holds is never overwritten: new content gets a new texture, and the
        // old one is destroyed when the last stored command using it goes.
        std::unordered_map<drivers::handle, std::uint32_t> store_refs_;
        std::unordered_set<drivers::handle> orphans_;

        bool held_by_store(const drivers::handle h) const;

    protected:
        std::uint64_t hash_bitwise_bitmap(epoc::bitwise_bitmap *bw_bmp);

    public:
        explicit bitmap_cache(kernel_system *kern_);

        const driver_texture_handle_array &texture_array() {
            return driver_textures;
        }

        const bitmap_array &bitwise_bitmap_array() {
            return bitmaps;
        }

        std::int64_t get_suitable_bitmap_index();

        /**
         * @brief   Add a bitmap to texture cache if not available in the cache, and get
         *          the driver's texture handle.
         * 
         * If the cache is full, this will find the least used bitmap (by sorting out 
         * last used timestamp). Also, since bitwise bitmap modify itself by user's will
         * without a method to notify the user, this also hashes bitmap data (using xxHash),
         * and will reupload the bitmap to driver if the texture data is different.
         * 
         * @param   driver          Pointer to graphics driver instance.
         * @param   bmp             The pointer to bitwise bitmap.
         * @param   builder         Pointer to a command builder, used for updating texture or destroying texture in sequence. NULL if not needed.
         * @param   update_cmd      Pointer to a store command that will be filled with bitmap updating plus destroying command. NULL if not needed.
         *
         * @returns Handle to driver's texture associated with this bitmap.
         */
        drivers::handle add_or_get(drivers::graphics_driver *driver, epoc::bitwise_bitmap *bmp,
            drivers::graphics_command_builder *builder = nullptr, gdi_store_command *update_cmd = nullptr);

        /**
         * \brief   Remove the bitmap from cache.
         * \returns True if success. False if bitmap not found. Likely that the bitmap has been
         *          purged from cache
         */
        bool remove(epoc::bitwise_bitmap *bmp);

        /**
         * @brief   A stored draw command now uses this texture: it keeps its content until released.
         */
        void retain(const drivers::handle h);

        /**
         * @brief   A stored draw command using this texture is gone. The last release of a texture the
         *          cache has already replaced for its bitmap destroys it.
         */
        void release(const drivers::handle h);

        std::size_t store_held_count() const {
            return store_refs_.size();
        }

        std::size_t orphan_count() const {
            return orphans_.size();
        }

        void clean(drivers::graphics_driver *drv);
    };
}
