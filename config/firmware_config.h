// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
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

struct M3Config {
    // QS printer defaults.  The MAC is configurable because the address on a
    // replacement printer may differ from the reference unit.
    std::array<char, 32> printer_name{{'Q', 's', 'p', 'r', 'i', 'n', 't', 'e', 'r', '\0'}};
    std::array<char, 8> pin{{'0', '0', '0', '0', '\0'}};
    std::array<std::uint8_t, 6> mac{{0x86, 0x67, 0x7a, 0xd8, 0x36, 0x6e}};
};

struct FirmwareConfig {
    DisplayConfig display{};
    M1Config m1{};
    M2Config m2{};
    M3Config m3{};
};

// The display section is required; M1, M2, and M3 settings are optional.
bool parse_firmware_config(std::string_view contents, FirmwareConfig& output);

}  // namespace config
