// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "m3/escpos.h"

#include <algorithm>

#include "gbr/format.h"

namespace m3 {

bool EscPosRasterEncoder::reset(const std::uint8_t* data, const std::size_t size,
                                const std::uint8_t palette) {
    data_ = data;
    size_ = size;
    palette_ = gbr::format::preview_palette(palette);
    band_index_ = 0;
    errors_.fill(0);
    next_errors_.fill(0);
    valid_ = data_ != nullptr && size_ != 0 && size_ % BYTES_PER_TILE_ROW == 0 &&
             size_ / BYTES_PER_TILE_ROW <= 4095;
    source_height_ = valid_ ? (size_ / BYTES_PER_TILE_ROW) * 8 : 0;
    return valid_;
}

std::uint8_t EscPosRasterEncoder::source_pixel(const std::size_t x, const std::size_t y) const {
    if (x >= SOURCE_WIDTH || y >= source_height_) return 0;
    const std::size_t tile_offset = (y / 8) * BYTES_PER_TILE_ROW + (x / 8) * 16 + (y % 8) * 2;
    const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (x % 8));
    return static_cast<std::uint8_t>(((data_[tile_offset] & mask) ? 1u : 0u) |
                                     ((data_[tile_offset + 1] & mask) ? 2u : 0u));
}

std::uint8_t EscPosRasterEncoder::gray_pixel(const std::size_t x, const std::size_t y) const {
    // The Game Boy palette is four two-bit shade entries, ordered from light
    // to dark in the display renderer.  Convert that level to 8-bit gray.
    const std::uint8_t shade = static_cast<std::uint8_t>((palette_ >> (source_pixel(x, y) * 2)) & 3u);
    return static_cast<std::uint8_t>(255u - shade * 85u);
}

void EscPosRasterEncoder::dither_row(const std::size_t output_y, std::uint8_t* bits) {
    std::fill(bits, bits + OUTPUT_WIDTH, 0);
    for (std::size_t x = 0; x < OUTPUT_WIDTH; ++x) {
        const std::size_t source_y = output_y / 2;
        const std::size_t source_x = x / 2;
        const int adjusted = static_cast<int>(gray_pixel(source_x, source_y)) + errors_[x];
        const bool black = adjusted < 128;
        const int quantized = black ? 0 : 255;
        const int error = adjusted - quantized;
        if (black) bits[x] = 1;

        if (x + 1 < OUTPUT_WIDTH) errors_[x + 1] = static_cast<std::int16_t>(
            errors_[x + 1] + (error * 7) / 16);
        if (x > 0) next_errors_[x - 1] = static_cast<std::int16_t>(
            next_errors_[x - 1] + (error * 3) / 16);
        next_errors_[x] = static_cast<std::int16_t>(next_errors_[x] + (error * 5) / 16);
        if (x + 1 < OUTPUT_WIDTH) next_errors_[x + 1] = static_cast<std::int16_t>(
            next_errors_[x + 1] + error / 16);
    }
    errors_ = next_errors_;
    next_errors_.fill(0);
}

std::size_t EscPosRasterEncoder::encoded_size() const {
    const std::size_t full_bands = output_height() / DOT_BAND_HEIGHT;
    const std::size_t remaining_rows = output_height() % DOT_BAND_HEIGHT;
    return full_bands * BAND_BYTES +
           (remaining_rows == 0 ? 0 : RASTER_HEADER_BYTES + remaining_rows * RASTER_ROW_BYTES);
}

bool EscPosRasterEncoder::next_band(std::uint8_t* output, const std::size_t capacity,
                                    std::size_t& length) {
    length = 0;
    if (!valid_ || output == nullptr || capacity < BAND_BYTES || band_index_ >= band_count()) return false;

    std::fill(output, output + BAND_BYTES, 0);
    const std::size_t remaining_rows = output_height() - band_index_ * DOT_BAND_HEIGHT;
    if (remaining_rows < DOT_BAND_HEIGHT) {
        // ESC * always occupies 24 vertical dots, even when the final band
        // contains fewer image rows. GS v 0 prints the exact remaining height.
        output[0] = 0x1d;
        output[1] = 0x76;
        output[2] = 0x30;
        output[3] = 0x00;
        output[4] = static_cast<std::uint8_t>(RASTER_ROW_BYTES);
        output[5] = 0;
        output[6] = static_cast<std::uint8_t>(remaining_rows);
        output[7] = 0;
        std::array<std::uint8_t, OUTPUT_WIDTH> bits{};
        for (std::size_t row = 0; row < remaining_rows; ++row) {
            dither_row(band_index_ * DOT_BAND_HEIGHT + row, bits.data());
            for (std::size_t x = 0; x < OUTPUT_WIDTH; ++x) {
                if (bits[x] != 0) {
                    output[RASTER_HEADER_BYTES + row * RASTER_ROW_BYTES + x / 8] |=
                        static_cast<std::uint8_t>(0x80u >> (x % 8));
                }
            }
        }
        ++band_index_;
        length = RASTER_HEADER_BYTES + remaining_rows * RASTER_ROW_BYTES;
        return true;
    }
    output[0] = 0x1B;
    output[1] = 0x2A;
    output[2] = 33;  // 24-dot, double-density ESC * mode used by the QS sample.
    output[3] = static_cast<std::uint8_t>(OUTPUT_WIDTH & 0xff);
    output[4] = static_cast<std::uint8_t>(OUTPUT_WIDTH >> 8);

    std::array<std::uint8_t, OUTPUT_WIDTH> bits{};
    for (std::size_t row = 0; row < DOT_BAND_HEIGHT; ++row) {
        const std::size_t output_y = band_index_ * DOT_BAND_HEIGHT + row;
        dither_row(output_y, bits.data());
        for (std::size_t x = 0; x < OUTPUT_WIDTH; ++x) {
            if (bits[x] == 0) continue;
            const std::size_t byte_offset = 5 + x * 3 + row / 8;
            output[byte_offset] = static_cast<std::uint8_t>(
                output[byte_offset] | (1u << (7u - (row % 8))));
        }
    }
    output[5 + OUTPUT_WIDTH * 3] = '\n';
    ++band_index_;
    length = BAND_BYTES;
    return true;
}

}  // namespace m3
