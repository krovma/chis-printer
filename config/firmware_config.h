// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <string_view>

#include "types.h"
#include "display/zjy154s0800.h"
#include "gbp/host/link_timing.h"

namespace Config {

struct DisplayConfig {
    uint8 palette_key       = Display::WHITE_PALETTE_KEY;
    uint8 backlight_percent = Display::BACKLIGHT_DUTY_PERCENT[Display::DEFAULT_BACKLIGHT_KEY];
};

struct M1Config {
    uint32 auto_cut_time_limit_us = 1'000'000;
    uint32 simulated_print_time_us = 1'000'000;
};

struct M2Config {
    uint8 rx_sample_delay_us = Gbp::Host::RX_SAMPLE_DELAY_DEFAULT_US;
};

enum class PrinterType : uint8 { Mm58, Mm80 };

struct M3AdvanceConfig {
    // Zero selects the values associated with the printer type. Printable width is
    // used to validate that the image fits; ESC a handles centering on paper.
    uint16 printable_width_dots = 0;
    uint8 image_scale = 0;
};

struct M3AdvanceOverrides {
    M3AdvanceConfig mm58{};
    M3AdvanceConfig mm80{};
};

constexpr M3AdvanceConfig default_m3_advance(const PrinterType type) {
    switch (type) {
        case PrinterType::Mm58: return {384, 2};
        case PrinterType::Mm80: return {576, 3};
    }
    return {};
}

struct M3Config {
    // Discover the printer by its Classic Bluetooth name during pairing.
    std::array<char, 32> printer_name{{'Q', 's', 'p', 'r', 'i', 'n', 't', 'e', 'r', '\0'}};
    std::array<char, 8> pin{{'0', '0', '0', '0', '\0'}};
    PrinterType printer_type = PrinterType::Mm58;
    M3AdvanceOverrides advance{};

    constexpr M3AdvanceConfig resolved_advance() const {
        M3AdvanceConfig settings = default_m3_advance(printer_type);
        const M3AdvanceConfig& overrides = printer_type == PrinterType::Mm80 ? advance.mm80 : advance.mm58;
        if (overrides.printable_width_dots != 0) {
            settings.printable_width_dots = overrides.printable_width_dots;
        }
        if (overrides.image_scale != 0) {
            settings.image_scale = overrides.image_scale;
        }
        return settings;
    }
};

struct FirmwareConfig {
    DisplayConfig display{};
    M1Config m1{};
    M2Config m2{};
    M3Config m3{};
};

// The display section is required; M1, M2, and M3 settings are optional.
bool parse_firmware_config(std::string_view contents, FirmwareConfig& output);

}  // namespace Config
