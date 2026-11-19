// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace m3 {

// A 48 mm, 203 dpi mechanism exposes roughly 384 dots.  The Game Boy image
// is 160 dots wide, so M3 uses a 2x nearest-neighbour enlargement to 320 dots
// and leaves a small, well-defined side margin on the mechanism.
inline constexpr std::size_t SOURCE_WIDTH = 160;
inline constexpr std::size_t OUTPUT_WIDTH = 320;
inline constexpr std::size_t DOT_BAND_HEIGHT = 24;
inline constexpr std::size_t BYTES_PER_TILE_ROW = 20 * 16;
inline constexpr std::size_t RASTER_ROW_BYTES = OUTPUT_WIDTH / 8;
inline constexpr std::size_t RASTER_HEADER_BYTES = 8;
// Maximum size: a full-height ESC * band followed by LF.
inline constexpr std::size_t BAND_BYTES = 5 + OUTPUT_WIDTH * 3 + 1;

class EscPosRasterEncoder {
  public:
    EscPosRasterEncoder() = default;
    EscPosRasterEncoder(const std::uint8_t* data, std::size_t size, std::uint8_t palette) {
        reset(data, size, palette);
    }

    bool reset(const std::uint8_t* data, std::size_t size, std::uint8_t palette);
    bool next_band(std::uint8_t* output, std::size_t capacity, std::size_t& length);
    std::size_t source_height() const { return source_height_; }
    std::size_t output_height() const { return source_height_ * 2; }
    std::size_t band_count() const { return (output_height() + DOT_BAND_HEIGHT - 1) / DOT_BAND_HEIGHT; }
    std::size_t encoded_size() const;
    std::size_t band_index() const { return band_index_; }
    bool valid() const { return valid_; }

  private:
    std::uint8_t source_pixel(std::size_t x, std::size_t y) const;
    std::uint8_t gray_pixel(std::size_t x, std::size_t y) const;
    void dither_row(std::size_t output_y, std::uint8_t* bits);

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t source_height_ = 0;
    std::uint8_t palette_ = 0;
    std::size_t band_index_ = 0;
    bool valid_ = false;
    std::array<std::int16_t, OUTPUT_WIDTH> errors_{};
    std::array<std::int16_t, OUTPUT_WIDTH> next_errors_{};
};

}  // namespace m3
