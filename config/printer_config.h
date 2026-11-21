// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"

namespace Config {

enum class PrinterType : uint8 { Mm58, Mm80 };

struct M3AdvanceConfig {
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
    std::array<char, 32> printer_name{};
    std::array<char, 8> pin{};
    PrinterType printer_type = PrinterType::Mm58;
    M3AdvanceOverrides advance{};

    constexpr M3AdvanceConfig resolved_advance() const {
        M3AdvanceConfig settings = default_m3_advance(printer_type);
        const M3AdvanceConfig& overrides = printer_type == PrinterType::Mm80 ? advance.mm80 : advance.mm58;
        if (overrides.printable_width_dots != 0) settings.printable_width_dots = overrides.printable_width_dots;
        if (overrides.image_scale != 0) settings.image_scale = overrides.image_scale;
        return settings;
    }
};

}  // namespace Config
