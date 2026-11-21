// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "btprinter/escpos.h"

#include <algorithm>

#include "types.h"
#include "gbr/format.h"

namespace BtPrinter {

bool EscPosRasterEncoder::reset(const uint8* data, const usize size,
                                const uint8 palette, const uint8 scale) {
    data_ = data;
    size_ = size;
    palette_ = Gbr::Format::preview_palette(palette);
    scale_ = scale;
    band_index_ = 0;
    errors_.fill(0);
    next_errors_.fill(0);
    valid_ = valid_image_scale(scale_) && data_ != nullptr && size_ != 0 &&
             size_ % BYTES_PER_TILE_ROW == 0 &&
             size_ / BYTES_PER_TILE_ROW <= 4095;
    source_height_ = valid_ ? (size_ / BYTES_PER_TILE_ROW) * 8 : 0;
    return valid_;
}

uint8 EscPosRasterEncoder::source_pixel(const usize x, const usize y) const {
    if (x >= SOURCE_WIDTH || y >= source_height_) {
        return 0;
    }
    const usize tile_offset = (y / 8) * BYTES_PER_TILE_ROW + (x / 8) * 16 + (y % 8) * 2;
    const uint8 mask = static_cast<uint8>(0x80u >> (x % 8));
    return static_cast<uint8>(((data_[tile_offset] & mask) ? 1u : 0u) |
                                     ((data_[tile_offset + 1] & mask) ? 2u : 0u));
}

uint8 EscPosRasterEncoder::gray_pixel(const usize x, const usize y) const {
    // The Game Boy palette is four two-bit shade entries, ordered from light
    // to dark in the display renderer.  Convert that level to 8-bit gray.
    const uint8 shade = static_cast<uint8>((palette_ >> (source_pixel(x, y) * 2)) & 3u);
    return static_cast<uint8>(255u - shade * 85u);
}

void EscPosRasterEncoder::dither_row(const usize output_y, uint8* bits) {
    const usize width = output_width();
    std::fill(bits, bits + width, 0);
    for (usize x = 0; x < width; ++x) {
        const usize source_y = output_y / scale_;
        const usize source_x = x / scale_;
        const int adjusted = static_cast<int>(gray_pixel(source_x, source_y)) + errors_[x];
        const bool black = adjusted < 128;
        const int quantized = black ? 0 : 255;
        const int error = adjusted - quantized;
        if (black) {
            bits[x] = 1;
        }

        if (x + 1 < width) {
            errors_[x + 1] = static_cast<int16>(errors_[x + 1] + (error * 7) / 16);
        }
        if (x > 0) {
            next_errors_[x - 1] = static_cast<int16>(next_errors_[x - 1] + (error * 3) / 16);
        }
        next_errors_[x] = static_cast<int16>(next_errors_[x] + (error * 5) / 16);
        if (x + 1 < width) {
            next_errors_[x + 1] = static_cast<int16>(next_errors_[x + 1] + error / 16);
        }
    }
    errors_ = next_errors_;
    next_errors_.fill(0);
}

usize EscPosRasterEncoder::encoded_size() const {
    const usize full_bands = output_height() / DOT_BAND_HEIGHT;
    const usize remaining_rows = output_height() % DOT_BAND_HEIGHT;
    return full_bands * band_bytes() +
           (remaining_rows == 0 ? 0 : RASTER_HEADER_BYTES + remaining_rows * raster_row_bytes());
}

bool EscPosRasterEncoder::next_band(uint8* output, const usize capacity,
                                    usize& length) {
    length = 0;
    if (!valid_ || output == nullptr || band_index_ >= band_count()) {
        return false;
    }

    const usize remaining_rows = output_height() - band_index_ * DOT_BAND_HEIGHT;
    const usize width = output_width();
    const usize row_bytes = raster_row_bytes();
    const usize required = remaining_rows < DOT_BAND_HEIGHT ?
        RASTER_HEADER_BYTES + remaining_rows * row_bytes : band_bytes();
    if (capacity < required) {
        return false;
    }
    std::fill(output, output + required, 0);
    if (remaining_rows < DOT_BAND_HEIGHT) {
        // GS v 0 m xL xH yL yH d... (1D 76 30 00 ...): print raster bitmap
        // in normal density. x is bytes per row; y is the exact tail height.
        output[0] = 0x1d;
        output[1] = 0x76;
        output[2] = 0x30;
        output[3] = 0x00;
        output[4] = static_cast<uint8>(row_bytes & 0xff);
        output[5] = static_cast<uint8>(row_bytes >> 8);
        output[6] = static_cast<uint8>(remaining_rows);
        output[7] = 0;
        for (usize row = 0; row < remaining_rows; ++row) {
            dither_row(band_index_ * DOT_BAND_HEIGHT + row, row_bits_.data());
            for (usize x = 0; x < width; ++x) {
                if (row_bits_[x] != 0) {
                    output[RASTER_HEADER_BYTES + row * row_bytes + x / 8] |=
                        static_cast<uint8>(0x80u >> (x % 8));
                }
            }
        }
        ++band_index_;
        length = required;
        return true;
    }
    // ESC * m nL nH d... (1B 2A 21 ...): select 24-dot double-density
    // bit-image mode; nL + 256*nH is the width in dots, with 3 bytes per dot.
    output[0] = 0x1B;
    output[1] = 0x2A;
    output[2] = 33;
    output[3] = static_cast<uint8>(width & 0xff);
    output[4] = static_cast<uint8>(width >> 8);

    for (usize row = 0; row < DOT_BAND_HEIGHT; ++row) {
        const usize output_y = band_index_ * DOT_BAND_HEIGHT + row;
        dither_row(output_y, row_bits_.data());
        for (usize x = 0; x < width; ++x) {
            if (row_bits_[x] == 0) {
                continue;
            }
            const usize byte_offset = 5 + x * 3 + row / 8;
            output[byte_offset] = static_cast<uint8>(
                output[byte_offset] | (1u << (7u - (row % 8))));
        }
    }
    // LF (0A): print the completed ESC * band and advance at zero line spacing.
    output[required - 1] = '\n';
    ++band_index_;
    length = required;
    return true;
}

}  // namespace BtPrinter
