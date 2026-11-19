// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "display/ui/screen.h"

#include <cstdint>

#include "display/ui/text.h"

namespace display::ui {

bool draw_centered_text(const std::string_view text, const Gray4 background, const Gray4 foreground,
                        const std::int16_t letter_spacing) {
    const std::int16_t width = measure_utf8(text, letter_spacing);
    if (width < 0 || width > WIDTH) {
        return false;
    }

    display::clear(background);
    const std::int16_t x = static_cast<std::int16_t>((static_cast<std::int32_t>(WIDTH) - width) / 2);
    const std::int16_t y = static_cast<std::int16_t>((static_cast<std::int32_t>(HEIGHT) - 16) / 2);
    return draw_utf8(text, x, y, foreground, letter_spacing);
}

}  // namespace display::ui
