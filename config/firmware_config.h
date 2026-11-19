// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>
#include <string_view>

#include "display/zjy154s0800.h"
#include "gbp/host/link_timing.h"

namespace config {

struct DisplayConfig {
    std::uint8_t palette_key       = display::WHITE_PALETTE_KEY;
    std::uint8_t backlight_percent = display::BACKLIGHT_DUTY_PERCENT[display::DEFAULT_BACKLIGHT_KEY];
};

struct M1Config {
    std::uint32_t auto_cut_time_limit_us = 1'000'000;
};

struct M2Config {
    std::uint8_t rx_sample_delay_us = gbp::host::RX_SAMPLE_DELAY_DEFAULT_US;
};

struct FirmwareConfig {
    DisplayConfig display{};
    M1Config m1{};
    M2Config m2{};
};

// The display section is required; M1 and M2 settings are optional.
bool parse_firmware_config(std::string_view contents, FirmwareConfig& output);

}  // namespace config
