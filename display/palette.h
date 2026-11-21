// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"

namespace Display {

inline constexpr uint8 WHITE_PALETTE_KEY = 6;
inline constexpr usize  PALETTE_COUNT     = WHITE_PALETTE_KEY + 1;

// Keys 0..5 are HSV hues 0, 60, ..., 300 degrees at S=V=1; key 6 is white.
// Every channel is either fully on or fully off in RGB444.
inline constexpr std::array<uint16, PALETTE_COUNT> BASE_PALETTE_RGB444{
    0xF00, 0xFF0, 0x0F0, 0x0FF, 0x00F, 0xF0F, 0xFFF,
};

namespace Detail {

constexpr uint16 tint_rgb444(const uint8 gray, const uint16 base) {
    // Base channels are 0 or 0xF, so this equals gray/15 * base per channel.
    return static_cast<uint16>((static_cast<uint16>(gray & 0x0F) * 0x111) & base);
}

inline void pack_gray_pair(const uint8 packed_gray, const uint16 base, uint8* target) {
    const uint16 first  = tint_rgb444(packed_gray >> 4, base);
    const uint16 second = tint_rgb444(packed_gray & 0x0F, base);

    // ST7789 RGB444 serial order: [R1 G1] [B1 R2] [G2 B2].
    target[0] = static_cast<uint8>(first >> 4);
    target[1] = static_cast<uint8>((first << 4) | (second >> 8));
    target[2] = static_cast<uint8>(second);
}

}  // namespace Detail
}  // namespace Display
