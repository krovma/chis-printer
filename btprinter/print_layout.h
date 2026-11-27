// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"
#include "config/printer_config.h"

namespace BtPrinter {

inline constexpr uint16 TSPL_ROLL_EXTRA_LENGTH_MM = 15;

// 203 dpi = 2030 / 254 dots per millimeter, rounded to the nearest dot.
constexpr usize millimeters_to_dots(const uint16 millimeters) {
    return (static_cast<usize>(millimeters) * 2030u + 127u) / 254u;
}

constexpr bool tspl_roll_label_length_mm(const usize print_rows, const uint8 scale_quarters,
                                         uint16& length_mm) {
    const usize image_height_dots = Config::scaled_dots(print_rows, scale_quarters);
    // Invert millimeters_to_dots so the TSPL SIZE value rounds back to the raster height.
    const usize image_height_mm = (image_height_dots * 254u + 1902u) / 2030u;
    const usize total_length_mm = image_height_mm + TSPL_ROLL_EXTRA_LENGTH_MM;
    if (scale_quarters == 0 || total_length_mm > Config::MAX_LABEL_LENGTH_MM) {
        return false;
    }
    length_mm = static_cast<uint16>(total_length_mm);
    return true;
}

struct LabelLayout {
    usize content_dots = 0;
    usize label_dots = 0;
    usize top_dots = 0;
    usize bottom_dots = 0;
    bool overflow = false;
};

constexpr LabelLayout layout_label(const usize source_rows, const usize inner_margin_rows,
                                   const uint8 scale_quarters, const uint16 length_mm) {
    const usize content = Config::scaled_dots(source_rows + inner_margin_rows, scale_quarters);
    const usize label = millimeters_to_dots(length_mm);
    if (content > label) return {content, label, 0, 0, true};
    const usize spare = label - content;
    return {content, label, spare / 2, spare - spare / 2, false};
}

}  // namespace BtPrinter
