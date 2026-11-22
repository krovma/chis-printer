// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "btprinter/dither.h"

namespace BtPrinter {

uint8 DitherSource::gray_pixel(const usize x, const usize y) const {
    if (x >= SOURCE_WIDTH || y >= height) return 255;
    const usize offset = (y / 8) * BYTES_PER_TILE_ROW + (x / 8) * 16 + (y % 8) * 2;
    const uint8 mask = static_cast<uint8>(0x80u >> (x % 8));
    const uint8 pixel = static_cast<uint8>(((data[offset] & mask) ? 1u : 0u) |
                                           ((data[offset + 1] & mask) ? 2u : 0u));
    const uint8 shade = static_cast<uint8>((palette >> (pixel * 2)) & 3u);
    return static_cast<uint8>(255u - shade * 85u);
}

void DitherWorkspace::reset() {
    errors.fill(0);
    next_errors.fill(0);
}

namespace {

class Bayer1x final : public DitherStrategy {
  public:
    uint8 scale_quarters() const override { return Config::SCALE_1X; }
    void row(const DitherSource& source, const usize y,
             uint8* bits, DitherWorkspace&) const override {
        constexpr uint8 matrix[4][4] = {
            {0, 8, 2, 10}, {12, 4, 14, 6},
            {3, 11, 1, 9}, {15, 7, 13, 5},
        };
        for (usize x = 0; x < SOURCE_WIDTH; ++x) {
            bits[x] = source.gray_pixel(x, y) < matrix[y % 4][x % 4] * 16u + 8u;
        }
    }
};

template <uint8 ScaleQuarters>
class FloydSteinberg final : public DitherStrategy {
  public:
    uint8 scale_quarters() const override { return ScaleQuarters; }
    void row(const DitherSource& source, const usize y,
             uint8* bits, DitherWorkspace& workspace) const override {
        constexpr usize width = Config::scaled_dots(SOURCE_WIDTH, ScaleQuarters);
        for (usize x = 0; x < width; ++x) {
            const int adjusted = static_cast<int>(source.gray_pixel(x * Config::SCALE_UNIT / ScaleQuarters,
                                                                     y * Config::SCALE_UNIT / ScaleQuarters)) +
                                 workspace.errors[x];
            const bool black = adjusted < 128;
            bits[x] = black;
            const int error = adjusted - (black ? 0 : 255);
            if (x + 1 < width) {
                workspace.errors[x + 1] = static_cast<int16>(workspace.errors[x + 1] + error * 7 / 16);
            }
            if (x != 0) {
                workspace.next_errors[x - 1] = static_cast<int16>(workspace.next_errors[x - 1] + error * 3 / 16);
            }
            workspace.next_errors[x] = static_cast<int16>(workspace.next_errors[x] + error * 5 / 16);
            if (x + 1 < width) {
                workspace.next_errors[x + 1] = static_cast<int16>(workspace.next_errors[x + 1] + error / 16);
            }
        }
        workspace.errors = workspace.next_errors;
        workspace.next_errors.fill(0);
    }
};

const Bayer1x BAYER_1X{};
const FloydSteinberg<Config::SCALE_1_5X> FS_1_5X{};
const FloydSteinberg<Config::SCALE_2X> FS_2X{};
const FloydSteinberg<Config::SCALE_3X> FS_3X{};
const FloydSteinberg<Config::SCALE_4X> FS_4X{};
const FloydSteinberg<Config::SCALE_5X> FS_5X{};

}  // namespace

const DitherStrategy* dither_strategy(const uint8 scale_quarters) {
    switch (scale_quarters) {
        case Config::SCALE_1X: return &BAYER_1X;
        case Config::SCALE_1_5X: return &FS_1_5X;
        case Config::SCALE_2X: return &FS_2X;
        case Config::SCALE_3X: return &FS_3X;
        case Config::SCALE_4X: return &FS_4X;
        case Config::SCALE_5X: return &FS_5X;
        default: return nullptr;
    }
}

}  // namespace BtPrinter
