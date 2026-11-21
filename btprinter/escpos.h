// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"

namespace BtPrinter {

// The 160-dot Game Boy image becomes 320 dots at 2x for the current 48 mm head,
// or 480 dots at 3x for a future 80 mm head. Both leave side margins.
inline constexpr usize SOURCE_WIDTH = 160;
inline constexpr uint8 DEFAULT_IMAGE_SCALE = 2;
inline constexpr uint8 MAX_IMAGE_SCALE = 3;
inline constexpr usize MAX_OUTPUT_WIDTH = SOURCE_WIDTH * MAX_IMAGE_SCALE;
inline constexpr usize DOT_BAND_HEIGHT = 24;
inline constexpr usize BYTES_PER_TILE_ROW = 20 * 16;
inline constexpr usize RASTER_HEADER_BYTES = 8;
// Maximum size: a full-height ESC * band followed by LF.
inline constexpr usize MAX_BAND_BYTES = 5 + MAX_OUTPUT_WIDTH * 3 + 1;
static_assert(MAX_OUTPUT_WIDTH <= 1023 && MAX_OUTPUT_WIDTH % 8 == 0);

// 1x does not provide the intended spatial resolution for four-level dithering.
constexpr bool valid_image_scale(const uint8 scale) { return scale == 2 || scale == 3; }

// Streams one dithered ESC/POS band at a time without storing a scaled image.
class EscPosRasterEncoder {
  public:
    EscPosRasterEncoder() = default;
    EscPosRasterEncoder(const uint8* data, usize size, uint8 palette,
                        uint8 scale = DEFAULT_IMAGE_SCALE) {
        reset(data, size, palette, scale);
    }

    bool reset(const uint8* data, usize size, uint8 palette,
               uint8 scale = DEFAULT_IMAGE_SCALE);
    // Returns false when the output buffer is too small or all bands are sent.
    bool next_band(uint8* output, usize capacity, usize& length);
    usize source_height() const { return source_height_; }
    usize output_width() const { return SOURCE_WIDTH * scale_; }
    usize output_height() const { return source_height_ * scale_; }
    usize raster_row_bytes() const { return output_width() / 8; }
    usize band_bytes() const { return 5 + output_width() * 3 + 1; }
    usize band_count() const { return (output_height() + DOT_BAND_HEIGHT - 1) / DOT_BAND_HEIGHT; }
    usize encoded_size() const;
    usize band_index() const { return band_index_; }
    bool valid() const { return valid_; }

  private:
    uint8 source_pixel(usize x, usize y) const;
    uint8 gray_pixel(usize x, usize y) const;
    void dither_row(usize output_y, uint8* bits);

    const uint8* data_ = nullptr;
    usize size_ = 0;
    usize source_height_ = 0;
    uint8 palette_ = 0;
    uint8 scale_ = DEFAULT_IMAGE_SCALE;
    usize band_index_ = 0;
    bool valid_ = false;
    std::array<int16, MAX_OUTPUT_WIDTH> errors_{};
    std::array<int16, MAX_OUTPUT_WIDTH> next_errors_{};
    std::array<uint8, MAX_OUTPUT_WIDTH> row_bits_{};
};

}  // namespace BtPrinter
