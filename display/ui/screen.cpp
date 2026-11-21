// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "display/ui/screen.h"
#include "types.h"

#include "display/ui/text.h"

namespace Display::Ui {

bool draw_centered_text(const std::string_view text, const Gray4 background, const Gray4 foreground,
                        const int16 letter_spacing) {
    const int16 width = measure_utf8(text, letter_spacing);
    if (width < 0 || width > WIDTH) {
        return false;
    }

    Display::clear(background);
    const int16 x = static_cast<int16>((static_cast<int32>(WIDTH) - width) / 2);
    const int16 y = static_cast<int16>((static_cast<int32>(HEIGHT) - 16) / 2);
    return draw_utf8(text, x, y, foreground, letter_spacing);
}

}  // namespace Display::Ui
