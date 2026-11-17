// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gbp::format {

// KGBR v2 stores the uncompressed Game Boy Printer tile stream. Each 8x8
// tile is 16 bytes (low/high bit planes, most-significant pixel first), with
// tiles ordered left-to-right in 8-pixel-high rows. Height is little-endian.
using PrintControl = std::array<std::uint8_t, 4>;
inline constexpr PrintControl DEFAULT_PRINT_CONTROL{1, 0x13, 0xE4, 0x40};
inline constexpr std::uint8_t GBR_VERSION = 2;
inline constexpr std::uint8_t GBR_WIDTH = 160;
inline constexpr std::size_t GBR_HEADER_SIZE = 12;

constexpr std::uint8_t preview_palette(const std::uint8_t raw) {
    if (raw == 0) return 0xE4;
    if (raw == 0xE1) return 0xD2;
    if (raw == 0x2D) return 0x1E;
    return raw;
}

constexpr std::array<std::uint8_t, GBR_HEADER_SIZE> make_gbr_header(const std::uint8_t width,
                                                                    const std::uint16_t height,
                                                                    const PrintControl& print_control) {
    return {'K', 'G', 'B', 'R', GBR_VERSION, width,
            static_cast<std::uint8_t>(height), static_cast<std::uint8_t>(height >> 8),
            print_control[0], print_control[1], print_control[2], print_control[3]};
}

}  // namespace gbp::format
