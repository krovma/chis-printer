// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace btprinter {

// The 160-dot Game Boy image becomes 320 dots at 2x for the current 48 mm head,
// or 480 dots at 3x for a future 80 mm head. Both leave side margins.
inline constexpr std::size_t SOURCE_WIDTH = 160;
inline constexpr std::uint8_t DEFAULT_IMAGE_SCALE = 2;
inline constexpr std::uint8_t MAX_IMAGE_SCALE = 3;
inline constexpr std::size_t MAX_OUTPUT_WIDTH = SOURCE_WIDTH * MAX_IMAGE_SCALE;
inline constexpr std::size_t DOT_BAND_HEIGHT = 24;
inline constexpr std::size_t BYTES_PER_TILE_ROW = 20 * 16;
inline constexpr std::size_t RASTER_HEADER_BYTES = 8;
// Maximum size: a full-height ESC * band followed by LF.
inline constexpr std::size_t MAX_BAND_BYTES = 5 + MAX_OUTPUT_WIDTH * 3 + 1;
static_assert(MAX_OUTPUT_WIDTH <= 1023 && MAX_OUTPUT_WIDTH % 8 == 0);

// 1x does not provide the intended spatial resolution for four-level dithering.
constexpr bool valid_image_scale(const std::uint8_t scale) { return scale == 2 || scale == 3; }

class EscPosRasterEncoder {
  public:
    EscPosRasterEncoder() = default;
    EscPosRasterEncoder(const std::uint8_t* data, std::size_t size, std::uint8_t palette,
                        std::uint8_t scale = DEFAULT_IMAGE_SCALE) {
        reset(data, size, palette, scale);
    }

    bool reset(const std::uint8_t* data, std::size_t size, std::uint8_t palette,
               std::uint8_t scale = DEFAULT_IMAGE_SCALE);
    bool next_band(std::uint8_t* output, std::size_t capacity, std::size_t& length);
    std::size_t source_height() const { return source_height_; }
    std::size_t output_width() const { return SOURCE_WIDTH * scale_; }
    std::size_t output_height() const { return source_height_ * scale_; }
    std::size_t raster_row_bytes() const { return output_width() / 8; }
    std::size_t band_bytes() const { return 5 + output_width() * 3 + 1; }
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
    std::uint8_t scale_ = DEFAULT_IMAGE_SCALE;
    std::size_t band_index_ = 0;
    bool valid_ = false;
    std::array<std::int16_t, MAX_OUTPUT_WIDTH> errors_{};
    std::array<std::int16_t, MAX_OUTPUT_WIDTH> next_errors_{};
    std::array<std::uint8_t, MAX_OUTPUT_WIDTH> row_bits_{};
};

}  // namespace btprinter
