// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"
#include "gbr/format.h"

namespace Config {

enum class PrinterType : uint8 { Mm37, Mm58, Mm80, Mm110, Count };
enum class PaperType : uint8 { Roll, Label };
enum class PrintProtocol : uint8 { EscPos };
enum class CenteringMode : uint8 { Printer, Firmware };

inline constexpr usize PRINTER_TYPE_COUNT = static_cast<usize>(PrinterType::Count);
inline constexpr std::array<const char*, PRINTER_TYPE_COUNT> PRINTER_TYPE_NAMES{
    "37mm", "58mm", "80mm", "110mm",
};
inline constexpr uint16 DEFAULT_LABEL_LENGTH_MM = 50;
inline constexpr uint16 MAX_LABEL_LENGTH_MM = 1000;
inline constexpr uint16 MAX_LABEL_GAP_MM = 100;
// Store every scale in quarter units so 1.5x stays exact in pixel and margin arithmetic.
inline constexpr uint8 SCALE_UNIT = 4;
inline constexpr uint8 SCALE_1X = 4;
inline constexpr uint8 SCALE_1_5X = 6;
inline constexpr uint8 SCALE_2X = 8;
inline constexpr uint8 SCALE_3X = 12;
inline constexpr uint8 SCALE_4X = 16;
inline constexpr uint8 SCALE_5X = 20;
inline constexpr uint16 AUTO_LEFT_MARGIN = 0xffff;
inline constexpr uint16 MAX_LEFT_MARGIN_DOTS = 1023;

constexpr usize scaled_dots(const usize dots, const uint8 scale_quarters) {
    return dots * scale_quarters / SCALE_UNIT;
}

constexpr uint16 default_left_margin_dots(const uint16 printable_width_dots, const uint8 scale_quarters) {
    const usize image_width = scaled_dots(Gbr::Format::GBR_WIDTH, scale_quarters);
    return printable_width_dots >= image_width ?
        static_cast<uint16>((printable_width_dots - image_width) / 2) : 0;
}

struct M3AdvanceConfig {
    uint16 printable_width_dots = 0;
    uint8 image_scale_quarters = 0;
    uint16 left_margin_dots = AUTO_LEFT_MARGIN;
};

struct M3AdvanceOverrides {
    M3AdvanceConfig mm37{};
    M3AdvanceConfig mm58{};
    M3AdvanceConfig mm80{};
    M3AdvanceConfig mm110{};
};

constexpr M3AdvanceConfig default_m3_advance(const PrinterType type) {
    switch (type) {
        case PrinterType::Mm37: return {280, SCALE_1X};  // 35 mm printable at 8 dots/mm.
        case PrinterType::Mm58: return {384, SCALE_2X};  // 48 mm printable at 8 dots/mm.
        case PrinterType::Mm80: return {576, SCALE_3X};  // 72 mm assumed printable at 8 dots/mm.
        case PrinterType::Mm110: return {831, SCALE_5X}; // 104 mm printable at 203 dpi, rounded.
        case PrinterType::Count: break;
    }
    return {};
}

constexpr bool printer_type_supports_scale(const PrinterType type, const uint8 scale_quarters) {
    if (type == PrinterType::Mm37) return scale_quarters == SCALE_1X || scale_quarters == SCALE_1_5X;
    if (type == PrinterType::Mm58) return scale_quarters == SCALE_1X || scale_quarters == SCALE_1_5X ||
                                         scale_quarters == SCALE_2X;
    if (type == PrinterType::Mm80) return scale_quarters == SCALE_1X || scale_quarters == SCALE_2X ||
                                         scale_quarters == SCALE_3X;
    if (type == PrinterType::Mm110) return scale_quarters >= SCALE_1X &&
                                          scale_quarters <= SCALE_5X && scale_quarters % SCALE_UNIT == 0;
    return false;
}

struct M3Config {
    std::array<char, 32> printer_name{};
    std::array<char, 8> pin{};
    PrinterType printer_type = PrinterType::Mm58;
    PaperType paper_type = PaperType::Roll;
    PrintProtocol protocol = PrintProtocol::EscPos;
    CenteringMode centering = CenteringMode::Printer;
    uint16 label_length_mm = DEFAULT_LABEL_LENGTH_MM;
    uint16 label_gap_mm = 0;
    M3AdvanceOverrides advance{};

    constexpr M3AdvanceConfig resolved_advance() const {
        M3AdvanceConfig settings = default_m3_advance(printer_type);
        const M3AdvanceConfig* overrides = nullptr;
        switch (printer_type) {
            case PrinterType::Mm37: overrides = &advance.mm37; break;
            case PrinterType::Mm58: overrides = &advance.mm58; break;
            case PrinterType::Mm80: overrides = &advance.mm80; break;
            case PrinterType::Mm110: overrides = &advance.mm110; break;
            default: return settings;
        }
        if (overrides->printable_width_dots != 0) settings.printable_width_dots = overrides->printable_width_dots;
        if (printer_type_supports_scale(printer_type, overrides->image_scale_quarters) &&
            scaled_dots(Gbr::Format::GBR_WIDTH, overrides->image_scale_quarters) <= settings.printable_width_dots) {
            settings.image_scale_quarters = overrides->image_scale_quarters;
        }
        settings.left_margin_dots = overrides->left_margin_dots == AUTO_LEFT_MARGIN ?
            default_left_margin_dots(settings.printable_width_dots, settings.image_scale_quarters) :
            overrides->left_margin_dots;
        return settings;
    }
};

}  // namespace Config
