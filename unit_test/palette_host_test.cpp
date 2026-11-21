// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <array>
#include <cassert>
#include <cmath>

#include "types.h"
#include "display/palette.h"

namespace {

std::array<double, 3> hsv_rgb(const uint8 key) {
    const double sector = static_cast<double>(key);
    const double x      = 1.0 - std::abs(std::fmod(sector, 2.0) - 1.0);
    switch (key) {
        case 0: return {1.0, x, 0.0};
        case 1: return {x, 1.0, 0.0};
        case 2: return {0.0, 1.0, x};
        case 3: return {0.0, x, 1.0};
        case 4: return {x, 0.0, 1.0};
        default: return {1.0, 0.0, x};
    }
}

uint16 quantize_rgb444(const std::array<double, 3>& rgb, const uint8 gray) {
    return static_cast<uint16>(
        (std::lround(gray * rgb[0]) << 8) |
        (std::lround(gray * rgb[1]) << 4) |
        std::lround(gray * rgb[2]));
}

}  // namespace

int main() {
    static_assert(Display::PALETTE_COUNT == 7);
    static_assert(Display::WHITE_PALETTE_KEY == 6);
    assert((Display::BASE_PALETTE_RGB444 ==
            std::array<uint16, 7>{0xF00, 0xFF0, 0x0F0, 0x0FF, 0x00F, 0xF0F, 0xFFF}));

    for (uint8 key = 0; key < Display::WHITE_PALETTE_KEY; ++key) {
        const auto rgb = hsv_rgb(key);
        assert(Display::BASE_PALETTE_RGB444[key] == quantize_rgb444(rgb, 15));
        for (uint8 gray = 0; gray < 16; ++gray) {
            assert(Display::Detail::tint_rgb444(gray, Display::BASE_PALETTE_RGB444[key]) == quantize_rgb444(rgb, gray));
        }
    }

    assert(Display::BASE_PALETTE_RGB444[Display::WHITE_PALETTE_KEY] == 0xFFF);
    for (uint8 gray = 0; gray < 16; ++gray) {
        assert(Display::Detail::tint_rgb444(gray, Display::BASE_PALETTE_RGB444[Display::WHITE_PALETTE_KEY]) == gray * 0x111);
    }

    for (uint16 packed = 0; packed <= 0xFF; ++packed) {
        std::array<uint8, 3> output{};
        Display::Detail::pack_gray_pair(static_cast<uint8>(packed), 0xFFF, output.data());
        assert(output[0] == (packed >> 4) * 0x11);
        assert(output[1] == packed);
        assert(output[2] == (packed & 0x0F) * 0x11);
    }

    std::array<uint8, 3> output{};
    Display::Detail::pack_gray_pair(0xF8, Display::BASE_PALETTE_RGB444[1], output.data());
    assert((output == std::array<uint8, 3>{0xFF, 0x08, 0x80}));
}
