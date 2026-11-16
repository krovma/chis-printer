// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace display {

inline constexpr uint8_t WHITE_PALETTE_KEY = 6;
inline constexpr size_t  PALETTE_COUNT     = WHITE_PALETTE_KEY + 1;

// Keys 0..5 are HSV hues 0, 60, ..., 300 degrees at S=V=1; key 6 is white.
// Every channel is either fully on or fully off in RGB444.
inline constexpr std::array<uint16_t, PALETTE_COUNT> BASE_PALETTE_RGB444{
    0xF00, 0xFF0, 0x0F0, 0x0FF, 0x00F, 0xF0F, 0xFFF,
};

namespace detail {

constexpr uint16_t tint_rgb444(const uint8_t gray, const uint16_t base) {
    // Base channels are 0 or 0xF, so this equals gray/15 * base per channel.
    return static_cast<uint16_t>((static_cast<uint16_t>(gray & 0x0F) * 0x111) & base);
}

inline void pack_gray_pair(const uint8_t packed_gray, const uint16_t base, uint8_t* target) {
    const uint16_t first  = tint_rgb444(packed_gray >> 4, base);
    const uint16_t second = tint_rgb444(packed_gray & 0x0F, base);

    // ST7789 RGB444 serial order: [R1 G1] [B1 R2] [G2 B2].
    target[0] = static_cast<uint8_t>(first >> 4);
    target[1] = static_cast<uint8_t>((first << 4) | (second >> 8));
    target[2] = static_cast<uint8_t>(second);
}

}  // namespace detail
}  // namespace display
