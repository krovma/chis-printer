// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>
#include <string_view>

#include "display/zjy154s0800.h"

namespace display::ui {

// Clear the display and draw text in the center of the 240 x 240 screen.
// The caller submits the resulting frame with present().
bool draw_centered_text(std::string_view text, Gray4 background, Gray4 foreground,
                        std::int16_t letter_spacing = 0);

}  // namespace display::ui
