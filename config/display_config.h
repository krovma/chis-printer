// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>
#include <string_view>

#include "display/zjy154s0800.h"

namespace config {

struct DisplayConfig {
    std::uint8_t palette_key       = display::WHITE_PALETTE_KEY;
    std::uint8_t backlight_percent = display::BACKLIGHT_DUTY_PERCENT[display::DEFAULT_BACKLIGHT_KEY];
};

// Apply only a complete, valid version-1 display section. UI keys are ignored.
bool parse_display_config(std::string_view contents, DisplayConfig& output);

}  // namespace config
