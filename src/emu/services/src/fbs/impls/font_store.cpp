/*
 * Copyright (c) 2019 EKA2L1 Team
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

#include <algorithm>
#include <vector>
#include <services/fbs/adapter/linked_font_adapter.h>
#include <services/fbs/font_store.h>

namespace eka2l1::epoc {
    static void fill_typeface_flags(const open_font_face_attrib &attrib, epoc::typeface_info &info) {
        if (attrib.style & epoc::open_font_face_attrib::serif) {
            info.flags |= epoc::typeface_info::tf_serif;
        }

        if (!(attrib.style & epoc::open_font_face_attrib::mono_width)) {
            info.flags |= epoc::typeface_info::tf_propotional;
        }

        if (attrib.style & epoc::open_font_face_attrib::symbol) {
            info.flags |= epoc::typeface_info::tf_symbol;
        }
    }

    bool font_store::add_fonts(std::vector<std::uint8_t> &buf, const epoc::adapter::font_file_adapter_kind adapter_kind,
        const bool user_font) {
        auto adapter = epoc::adapter::make_font_file_adapter(adapter_kind, buf);

        if (!adapter || !adapter->is_valid()) {
            return false;
        }

        for (std::size_t i = 0; i < adapter->count(); i++) {
            epoc::open_font_face_attrib attrib;

            if (!adapter->get_face_attrib(i, attrib)) {
                continue;
            }

            const std::u16string fam_name = attrib.fam_name.to_std_string(nullptr);
            const std::u16string name = attrib.name.to_std_string(nullptr);

            bool found = false;

            // Are we facing duplicate fonts ?
            for (std::size_t i = 0; i < open_font_store.size(); i++) {
                if (open_font_store[i].face_attrib.name.to_std_string(nullptr) == name) {
                    found = true;
                    break;
                }
            }

            // No duplicate font
            if (!found) {
                // Get the metrics and make new open font
                open_font_info info;

                info.family = fam_name;
                info.idx = static_cast<std::int32_t>(i);
                info.face_attrib = attrib;
                info.adapter = adapter.get();
                info.user_font = user_font;

                open_font_store.push_back(std::move(info));
            }

            // What CFontStore::TypefaceSupport reports. A bitmap (GDR) typeface carries its own TTypeface flags
            // and one height per font bitmap; the store answers with the smallest and largest of them and how
            // many distinct heights there are. An Open Font typeface takes its flags from the face attributes.
            // The entry used to be half uninitialised: the flags ORed into stack garbage and the maximum
            // height came from a metrics struct nothing filled, so a client choosing a typeface by its
            // attributes chose a different one on every run (Series 80 Sheet drew its cells in Digi24,
            // Terminal or Nokia12 by chance).
            std::uint32_t bitmap_flags = 0;
            std::vector<std::int32_t> bitmap_heights;
            const bool is_bitmap = !adapter->vectorizable() && adapter->get_bitmap_typeface_support(i, bitmap_flags, bitmap_heights);

            bool found_typeface = false;

            for (std::size_t j = 0; j < typefaces.size(); j++) {
                if (common::compare_ignore_case(typefaces[j].info_.name.to_std_string(nullptr), fam_name) == 0) {
                    // NOTE: Stored in here is actually pixels, they will just scale to twips later when retrieved through API
                    if (is_bitmap) {
                        std::vector<std::int32_t> &heights = typeface_bitmap_heights[j];
                        heights.insert(heights.end(), bitmap_heights.begin(), bitmap_heights.end());
                        std::sort(heights.begin(), heights.end());
                        heights.erase(std::unique(heights.begin(), heights.end()), heights.end());

                        typefaces[j].num_heights_ = static_cast<std::uint32_t>(heights.size());
                        typefaces[j].min_height_in_twips_ = heights.front();
                        typefaces[j].max_height_in_twips_ = heights.back();
                    } else {
                        typefaces[j].min_height_in_twips_ = common::min<std::int32_t>(typefaces[j].min_height_in_twips_, attrib.min_size_in_pixels);
                        fill_typeface_flags(attrib, typefaces[j].info_);
                    }

                    found_typeface = true;
                    break;
                }
            }

            if (!found_typeface) {
                epoc::typeface_support support{};
                support.info_.name.assign(nullptr, fam_name);
                support.info_.flags = 0;

                if (is_bitmap) {
                    support.info_.flags = bitmap_flags & (epoc::typeface_info::tf_propotional | epoc::typeface_info::tf_serif | epoc::typeface_info::tf_symbol);
                    support.num_heights_ = static_cast<std::uint32_t>(bitmap_heights.size());
                    support.is_scalable_ = 0;
                    support.min_height_in_twips_ = bitmap_heights.front();
                    support.max_height_in_twips_ = bitmap_heights.back();
                } else {
                    fill_typeface_flags(attrib, support.info_);

                    // The standard-size range an Open Font typeface reports (number of heights, largest height)
                    // is worked out from the minimum when a client asks (fbscli::typeface_support).
                    support.num_heights_ = 1;
                    support.is_scalable_ = adapter->vectorizable();
                    support.min_height_in_twips_ = std::max<std::int32_t>(1, attrib.min_size_in_pixels);
                    support.max_height_in_twips_ = support.min_height_in_twips_;
                }

                typefaces.push_back(std::move(support));
                typeface_bitmap_heights.push_back(is_bitmap ? bitmap_heights : std::vector<std::int32_t>{});
            }
        }

        font_adapters.push_back(std::move(adapter));
        return true;
    }

    bool font_store::add_linked_font(const std::u16string &name, const std::vector<std::u16string> &component_names,
        const std::size_t canonical) {
        if (component_names.empty() || (canonical >= component_names.size())) {
            return false;
        }

        for (auto &info : open_font_store) {
            if (common::compare_ignore_case(info.face_attrib.name.to_std_string(nullptr), name) == 0) {
                return false;
            }
        }

        std::vector<epoc::adapter::linked_font_file_adapter::component> components;
        open_font_info *canonical_info = nullptr;
        std::uint32_t coverage[4] = { 0, 0, 0, 0 };

        for (std::size_t i = 0; i < component_names.size(); i++) {
            open_font_info *found = nullptr;

            // link.ini names a component either in full ("Nokia Sans S60
            // SemiBold") or by family alone ("MHeiM-C-GB18030-S60"), the same
            // two spellings CFontStore::GetNearestOpenFontInPixelsByFontName
            // accepts and in the same order of preference.
            for (auto &info : open_font_store) {
                if (common::compare_ignore_case(info.face_attrib.name.to_std_string(nullptr), component_names[i]) == 0) {
                    found = &info;
                    break;
                }
            }

            if (!found) {
                for (auto &info : open_font_store) {
                    if (common::compare_ignore_case(info.face_attrib.fam_name.to_std_string(nullptr), component_names[i]) == 0) {
                        found = &info;
                        break;
                    }
                }
            }

            // A variant only ships the fonts it needs, while link.ini describes
            // every variant, so a missing component just means this typeface is
            // not for this device.
            if (!found) {
                return false;
            }

            if (i == canonical) {
                canonical_info = found;
            }

            for (std::size_t word = 0; word < 4; word++) {
                coverage[word] |= found->face_attrib.coverage[word];
            }

            components.push_back({ found->adapter, found->idx });
        }

        open_font_face_attrib attrib = canonical_info->face_attrib;
        const std::u16string canonical_family = attrib.fam_name.to_std_string(nullptr);
        attrib.name.assign(nullptr, name);
        attrib.fam_name.assign(nullptr, name);
        attrib.local_full_name.assign(nullptr, name);
        attrib.local_full_fam_name.assign(nullptr, name);
        std::copy(coverage, coverage + 4, attrib.coverage);

        auto adapter = std::make_unique<epoc::adapter::linked_font_file_adapter>(std::move(components), canonical, attrib);

        open_font_info info;
        info.family = name;
        info.idx = 0;
        info.face_attrib = attrib;
        info.adapter = adapter.get();

        // CFontStore::LoadFontsAtStartupL loads the linked fonts before the
        // rest of resource\\fonts, and equally good candidates are settled by
        // taking the first, so a linked typeface has to sit ahead of the plain
        // fonts it was assembled from. We can only build it once those are
        // loaded, hence the insert.
        open_font_store.insert(open_font_store.begin() + linked_font_count_, std::move(info));
        linked_font_count_++;

        // Present it to typeface enumeration the same way the canonical
        // component is, so a client listing typefaces can find it by name.
        epoc::typeface_support support{};
        std::vector<std::int32_t> support_heights;

        for (std::size_t j = 0; j < typefaces.size(); j++) {
            if (common::compare_ignore_case(typefaces[j].info_.name.to_std_string(nullptr), canonical_family) == 0) {
                support = typefaces[j];
                support_heights = typeface_bitmap_heights[j];
                break;
            }
        }

        support.info_.name.assign(nullptr, name);
        support.info_.flags = 0;
        fill_typeface_flags(attrib, support.info_);

        typefaces.push_back(std::move(support));
        typeface_bitmap_heights.push_back(std::move(support_heights));
        font_adapters.push_back(std::move(adapter));

        return true;
    }

    void font_store::attach_user_font_fallbacks() {
        struct fallback_font {
            epoc::adapter::linked_font_file_adapter::component component_;
            const open_font_face_attrib *attrib_;
        };

        std::vector<fallback_font> fallbacks;

        for (auto &info : open_font_store) {
            if (info.user_font) {
                fallbacks.push_back({ { info.adapter, info.idx }, &info.face_attrib });
            }
        }

        if (fallbacks.empty()) {
            return;
        }

        for (auto &info : open_font_store) {
            if (info.user_font) {
                continue;
            }

            std::vector<epoc::adapter::linked_font_file_adapter::component> components;
            components.push_back({ info.adapter, info.idx });

            open_font_face_attrib attrib = info.face_attrib;

            for (const auto &fallback : fallbacks) {
                // A glyph bitmap reaches the guest in the format the font as a
                // whole declares, so a component is only of use where the
                // linked adapter can present its glyphs as that format.
                if (!epoc::adapter::can_present_glyph_as(fallback.component_.adapter_->get_output_bitmap_type(),
                        info.adapter->get_output_bitmap_type())) {
                    continue;
                }

                components.push_back(fallback.component_);

                for (std::size_t word = 0; word < 4; word++) {
                    attrib.coverage[word] |= fallback.attrib_->coverage[word];
                }
            }

            if (components.size() == 1) {
                continue;
            }

            info.face_attrib = attrib;

            auto adapter = std::make_unique<epoc::adapter::linked_font_file_adapter>(std::move(components), 0, attrib);

            info.adapter = adapter.get();
            info.idx = 0;

            font_adapters.push_back(std::move(adapter));
        }
    }

    open_font_info *font_store::seek_the_font_by_uid(const epoc::uid the_uid, open_font_metrics &target_metric, std::uint32_t *metric_identifier) {
        for (auto &info : open_font_store) {
            if (std::optional<open_font_metrics> result = info.adapter->get_metric_with_uid(info.idx, the_uid, metric_identifier)) {
                target_metric = std::move(result.value());
                return &info;
            }
        }

        return nullptr;
    }

    static std::uint32_t coverage_extent(const open_font_face_attrib &attrib) {
        std::uint32_t bits = 0;

        for (const std::uint32_t word : attrib.coverage) {
            bits += common::count_bit_set(word);
        }

        return bits;
    }

    // Port of CFontStore's MatchFontSpecsInPixels (fontstore/src/FNTSTORE.CPP):
    // the higher the return value, the better the candidate. The weights are
    // Symbian's, deliberately small and close together -- a name match is worth
    // more than every style attribute put together, and being off by a few
    // pixels costs more than any of them.
    //
    // Two of Symbian's terms are missing rather than reweighted: the bitmap
    // type and the outline/shadow effects, neither of which a face attribute
    // carries here. They score every candidate identically, so the ranking is
    // the same without them.
    int match_font_spec(const open_font_face_attrib &candidate, const std::u16string &candidate_name,
        const epoc::font_spec_base &spec, const std::int32_t candidate_height) {
        static constexpr int SCORE_FOR_NAME = 10;
        static constexpr int MAX_SCORE_FOR_HEIGHT = 10;
        static constexpr int SCORE_FOR_ITALIC = 2;
        static constexpr int SCORE_FOR_BOLD = 2;
        static constexpr int SCORE_FOR_MONO_SPACE = 3;
        static constexpr int SCORE_FOR_SERIF = 1;

        const std::u16string wanted_name = const_cast<epoc::font_spec_base &>(spec).tf.name.to_std_string(nullptr);
        const std::uint32_t style = static_cast<const epoc::font_spec_v1 &>(spec).style.flags;
        const std::uint32_t typeface_flags = const_cast<epoc::font_spec_base &>(spec).tf.flags;

        int score = 0;

        if (!wanted_name.empty() && (common::compare_ignore_case(candidate_name, wanted_name) == 0)) {
            score += SCORE_FOR_NAME;
        }

        score += MAX_SCORE_FOR_HEIGHT - common::abs(spec.height - candidate_height);

        const bool candidate_italic = (candidate.style & epoc::open_font_face_attrib::italic);
        const bool candidate_bold = (candidate.style & epoc::open_font_face_attrib::bold);
        const bool candidate_mono = (candidate.style & epoc::open_font_face_attrib::mono_width);
        const bool candidate_serif = (candidate.style & epoc::open_font_face_attrib::serif);

        if (candidate_italic == static_cast<bool>(style & epoc::font_style_base::italic)) {
            score += SCORE_FOR_ITALIC;
        }

        if (candidate_bold == static_cast<bool>(style & epoc::font_style_base::bold)) {
            score += SCORE_FOR_BOLD;
        }

        if (candidate_mono == !(typeface_flags & epoc::typeface_info::tf_propotional)) {
            score += SCORE_FOR_MONO_SPACE;
        }

        if (candidate_serif == static_cast<bool>(typeface_flags & epoc::typeface_info::tf_serif)) {
            score += SCORE_FOR_SERIF;
        }

        return score;
    }

    // CFontStore::GetNearestFontToDesignHeightInPixels tries the requested name
    // first (GetNearestOpenFontInPixelsByFontName) and only ranks candidates
    // when that finds nothing (GetNearestOpenFontInPixelsBySimilarity).
    open_font_info *font_store::seek_the_open_font(epoc::font_spec_base &spec) {
        const std::u16string wanted_name = spec.tf.name.to_std_string(nullptr);
        const std::uint32_t style = static_cast<epoc::font_spec_v1 &>(spec).style.flags;

        if (!wanted_name.empty()) {
            const bool want_italic = (style & epoc::font_style_base::italic);
            const bool want_bold = (style & epoc::font_style_base::bold);

            // Full face name, then family name. Both also require the slant and
            // the weight to be the ones asked for, so "Nokia Sans S60" in the
            // regular does not answer a request for the bold.
            for (const bool by_family : { false, true }) {
                for (auto &info : open_font_store) {
                    const std::u16string candidate_name = by_family
                        ? info.face_attrib.fam_name.to_std_string(nullptr)
                        : info.face_attrib.name.to_std_string(nullptr);

                    if ((common::compare_ignore_case(candidate_name, wanted_name) == 0)
                        && (static_cast<bool>(info.face_attrib.style & epoc::open_font_face_attrib::italic) == want_italic)
                        && (static_cast<bool>(info.face_attrib.style & epoc::open_font_face_attrib::bold) == want_bold)) {
                        return &info;
                    }
                }
            }

            // A bitmap (GDR) typeface carries every weight and posture it has under one name, and its
            // attributes are the union of them, so it never equals a single-style request above. The
            // store picks a typeface by name and the style inside it (CFontStore::GetNearestTypeface,
            // GetNearestTypefaceFontBitmap): take it by name, the adapter chooses the bitmap.
            for (auto &info : open_font_store) {
                if (!info.adapter->vectorizable() && (common::compare_ignore_case(info.face_attrib.name.to_std_string(nullptr), wanted_name) == 0)) {
                    return &info;
                }
            }
        }

        open_font_info *best = nullptr;
        int best_score = 0;
        std::uint32_t best_extent = 0;

        for (auto &info : open_font_store) {
            std::optional<open_font_metrics> target_metric = info.adapter->get_nearest_supported_metric(info.idx,
                static_cast<std::uint16_t>(spec.height));

            const std::int32_t candidate_height = target_metric.has_value() ? target_metric->design_height : spec.height;
            // Symbian scores the name of the spec a font file reports, which
            // is the typeface name -- the family, not the full face name.
            const int score = match_font_spec(info.face_attrib, info.face_attrib.fam_name.to_std_string(nullptr), spec,
                candidate_height);

            // Symbian settles a tie by taking the candidate it met first, its
            // font files being in load order (CFontStore::LoadFontsAtStartupL,
            // which is also why linked typefaces sit at the front of the store
            // here). That order is the host filesystem's, not the device's, so
            // ties are broken by coverage instead: a request naming no typeface
            // should not be answered by a digits-only or symbol face just
            // because the directory happened to list it first.
            const std::uint32_t extent = coverage_extent(info.face_attrib);

            if (!best || (score > best_score) || ((score == best_score) && (extent > best_extent))) {
                best = &info;
                best_score = score;
                best_extent = extent;
            }
        }


        return best;
    }

    const std::vector<std::int32_t> &font_store::open_font_standard_sizes_in_twips() {
        static const std::vector<std::int32_t> sizes = []() {
            std::vector<std::int32_t> result;
            for (std::int32_t pt = 4; pt <= 18; pt++) {
                result.push_back(pt * 20);
            }
            for (std::int32_t pt = 20; pt <= 36; pt += 2) {
                result.push_back(pt * 20);
            }
            for (std::int32_t pt = 40; pt <= 72; pt += 4) {
                result.push_back(pt * 20);
            }
            for (std::int32_t pt = 80; pt <= 144; pt += 8) {
                result.push_back(pt * 20);
            }
            return result;
        }();

        return sizes;
    }

    std::size_t font_store::open_font_nearest_size_index(const std::int32_t min_height_in_twips) {
        const std::vector<std::int32_t> &sizes = open_font_standard_sizes_in_twips();
        std::size_t index = 0;

        for (; index < sizes.size(); index++) {
            if (min_height_in_twips <= sizes[index]) {
                break;
            }
        }

        return index;
    }

    std::size_t font_store::typeface_list_position(const std::uint32_t index) {
        if (index >= typefaces.size()) {
            return typefaces.size();
        }

        // CFontStore::TypefaceSupport lists the bitmap typefaces first, in the order they were loaded, and the
        // Open Font typefaces after them, sorted by name (CTypefaceSupportInfo::CompareFontNames).
        std::vector<std::size_t> order;
        order.reserve(typefaces.size());

        for (std::size_t i = 0; i < typefaces.size(); i++) {
            if (!typefaces[i].is_scalable_) {
                order.push_back(i);
            }
        }

        const std::size_t bitmap_count = order.size();

        for (std::size_t i = 0; i < typefaces.size(); i++) {
            if (typefaces[i].is_scalable_) {
                order.push_back(i);
            }
        }

        std::stable_sort(order.begin() + bitmap_count, order.end(), [this](const std::size_t a, const std::size_t b) {
            return common::compare_ignore_case(typefaces[a].info_.name.to_std_string(nullptr),
                       typefaces[b].info_.name.to_std_string(nullptr))
                < 0;
        });

        return order[index];
    }

    epoc::typeface_support *font_store::get_typeface_support(const std::uint32_t index) {
        const std::size_t pos = typeface_list_position(index);
        return (pos < typefaces.size()) ? &typefaces[pos] : nullptr;
    }

    const std::vector<std::int32_t> *font_store::get_typeface_bitmap_heights(const std::uint32_t index) {
        const std::size_t pos = typeface_list_position(index);
        if ((pos >= typefaces.size()) || typefaces[pos].is_scalable_ || typeface_bitmap_heights[pos].empty()) {
            return nullptr;
        }
        return &typeface_bitmap_heights[pos];
    }
}