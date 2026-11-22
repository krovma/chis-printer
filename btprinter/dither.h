// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"
#include "config/printer_config.h"

namespace BtPrinter {

inline constexpr usize SOURCE_WIDTH = Gbr::Format::GBR_WIDTH;
inline constexpr uint8 DEFAULT_IMAGE_SCALE_QUARTERS = Config::SCALE_2X;
inline constexpr usize MAX_OUTPUT_WIDTH = Config::scaled_dots(SOURCE_WIDTH, Config::SCALE_5X);
inline constexpr usize BYTES_PER_TILE_ROW = 20 * 16;

struct DitherSource {
    const uint8* data = nullptr;
    usize height = 0;
    uint8 palette = 0;
    uint8 gray_pixel(usize x, usize y) const;
};

struct DitherWorkspace {
    std::array<int16, MAX_OUTPUT_WIDTH> errors{};
    std::array<int16, MAX_OUTPUT_WIDTH> next_errors{};
    void reset();
};

class DitherStrategy {
  public:
    virtual ~DitherStrategy() = default;
    virtual uint8 scale_quarters() const = 0;
    virtual void row(const DitherSource& source, usize output_y,
                     uint8* bits, DitherWorkspace& workspace) const = 0;
};

const DitherStrategy* dither_strategy(uint8 scale_quarters);

}  // namespace BtPrinter
