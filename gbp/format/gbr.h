// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gbp::format {

// KGBR v1 stores the uncompressed Game Boy Printer tile stream. Each 8x8
// tile is 16 bytes (low/high bit planes, most-significant pixel first), with
// tiles ordered left-to-right in 8-pixel-high rows. Height is little-endian.
inline constexpr std::uint8_t GBR_VERSION = 1;
inline constexpr std::uint8_t GBR_WIDTH = 160;
inline constexpr std::size_t GBR_HEADER_SIZE = 8;

constexpr std::array<std::uint8_t, GBR_HEADER_SIZE> make_gbr_header(const std::uint8_t width,
                                                                    const std::uint16_t height) {
    return {'K', 'G', 'B', 'R', GBR_VERSION, width,
            static_cast<std::uint8_t>(height), static_cast<std::uint8_t>(height >> 8)};
}

}  // namespace gbp::format
