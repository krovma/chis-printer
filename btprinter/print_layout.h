// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"
#include "config/printer_config.h"

namespace BtPrinter {

// 203 dpi = 2030 / 254 dots per millimeter, rounded to the nearest dot.
constexpr usize millimeters_to_dots(const uint16 millimeters) {
    return (static_cast<usize>(millimeters) * 2030u + 127u) / 254u;
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
