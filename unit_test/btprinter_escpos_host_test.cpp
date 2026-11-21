// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <array>
#include <cassert>

#include "types.h"
#include "btprinter/escpos.h"

namespace {

usize black_dots(const std::array<uint8, BtPrinter::MAX_BAND_BYTES>& band,
                 const usize length) {
    usize count = 0;
    const usize first = band[0] == 0x1d ? BtPrinter::RASTER_HEADER_BYTES : 5;
    const usize last = band[0] == 0x1d ? length : length - 1;
    for (usize offset = first; offset < last; ++offset) {
        for (uint8 bit = 0; bit < 8; ++bit) {
            count += (band[offset] >> bit) & 1u;
        }
    }
    return count;
}

void check_band_header(const std::array<uint8, BtPrinter::MAX_BAND_BYTES>& band,
                       const usize width, const usize length) {
    assert(band[0] == 0x1b && band[1] == 0x2a && band[2] == 33);
    assert(band[3] == (width & 0xff) && band[4] == (width >> 8));
    assert(length == 5 + width * 3 + 1);
    assert(band[length - 1] == '\n');
}

void check_raster_header(const std::array<uint8, BtPrinter::MAX_BAND_BYTES>& band,
                         const usize width, const usize rows, const usize length) {
    assert(band[0] == 0x1d && band[1] == 0x76 && band[2] == 0x30 && band[3] == 0);
    assert(band[4] == width / 8 && band[5] == 0);
    assert(band[6] == rows && band[7] == 0);
    assert(length == BtPrinter::RASTER_HEADER_BYTES + rows * width / 8);
}

}  // namespace

int main() {
    constexpr usize width_2x = BtPrinter::SOURCE_WIDTH * 2;
    constexpr usize width_3x = BtPrinter::SOURCE_WIDTH * 3;
    std::array<uint8, BtPrinter::BYTES_PER_TILE_ROW> image{};
    std::array<uint8, BtPrinter::MAX_BAND_BYTES> band{};
    BtPrinter::EscPosRasterEncoder encoder;
    usize length = 0;

    assert(encoder.reset(image.data(), image.size(), 0xE4));
    assert(encoder.output_width() == width_2x);
    assert(encoder.band_count() == 1);
    assert(encoder.encoded_size() == 8 + 16 * 40);
    assert(!encoder.next_band(band.data(), encoder.encoded_size() - 1, length));
    assert(encoder.next_band(band.data(), encoder.encoded_size(), length));
    check_raster_header(band, width_2x, 16, length);
    assert(black_dots(band, length) == 0);
    assert(!encoder.next_band(band.data(), band.size(), length));

    image.fill(0xff);
    assert(encoder.reset(image.data(), image.size(), 0xE4));
    assert(encoder.next_band(band.data(), band.size(), length));
    check_raster_header(band, width_2x, 16, length);
    assert(black_dots(band, length) == width_2x * 16);
    for (usize offset = BtPrinter::RASTER_HEADER_BYTES; offset < length; ++offset) {
        assert(band[offset] == 0xff);
    }

    image.fill(0);
    image[0] = image[1] = 0x80;  // Top-left source pixel.
    image[318] = image[319] = 0x01;  // Bottom-right source pixel.
    assert(encoder.reset(image.data(), image.size(), 0xE4));
    assert(encoder.next_band(band.data(), band.size(), length));
    assert(black_dots(band, length) == 8);
    assert(band[8] == 0xc0 && band[8 + 40] == 0xc0);
    assert(band[8 + 14 * 40 + 39] == 0x03);
    assert(band[8 + 15 * 40 + 39] == 0x03);

    image.fill(0);
    for (usize i = 0; i < image.size(); i += 2) {
        image[i] = 0xff;
    }
    assert(encoder.reset(image.data(), image.size(), 0xE4));
    assert(encoder.next_band(band.data(), band.size(), length));
    const usize gray_dots = black_dots(band, length);
    assert(gray_dots > 0 && gray_dots < width_2x * 16);

    std::array<uint8, BtPrinter::BYTES_PER_TILE_ROW * 2> tall_image{};
    tall_image.fill(0xff);
    assert(encoder.reset(tall_image.data(), tall_image.size(), 0xE4));
    assert(encoder.band_count() == 2);
    assert(encoder.encoded_size() == 5 + width_2x * 3 + 1 + 8 + 8 * 40);
    assert(encoder.next_band(band.data(), band.size(), length));
    check_band_header(band, width_2x, length);
    assert(black_dots(band, length) == width_2x * 24);
    assert(encoder.next_band(band.data(), band.size(), length));
    check_raster_header(band, width_2x, 8, length);
    assert(black_dots(band, length) == width_2x * 8);
    assert(!encoder.next_band(band.data(), band.size(), length));

    assert(!encoder.reset(image.data(), image.size(), 0xE4, 1));
    assert(!encoder.reset(image.data(), image.size(), 0xE4, 4));

    image.fill(0xff);
    assert(encoder.reset(image.data(), image.size(), 0xE4, 3));
    assert(encoder.output_width() == width_3x);
    assert(encoder.output_height() == 24);
    assert(encoder.band_count() == 1);
    assert(encoder.encoded_size() == BtPrinter::MAX_BAND_BYTES);
    assert(!encoder.next_band(band.data(), band.size() - 1, length));
    assert(encoder.next_band(band.data(), band.size(), length));
    check_band_header(band, width_3x, length);
    assert(black_dots(band, length) == width_3x * 24);
    assert(!encoder.next_band(band.data(), band.size(), length));

    image.fill(0);
    image[0] = image[1] = 0x80;
    image[318] = image[319] = 0x01;
    assert(encoder.reset(image.data(), image.size(), 0xE4, 3));
    assert(encoder.next_band(band.data(), band.size(), length));
    assert(black_dots(band, length) == 18);
    assert(band[5] == 0xe0 && band[5 + 3] == 0xe0 && band[5 + 6] == 0xe0);
    assert(band[5 + 477 * 3 + 2] == 0x07 && band[5 + 479 * 3 + 2] == 0x07);

    tall_image.fill(0xff);
    assert(encoder.reset(tall_image.data(), tall_image.size(), 0xE4, 3));
    assert(encoder.band_count() == 2);
    assert(encoder.encoded_size() == 2 * BtPrinter::MAX_BAND_BYTES);
    assert(encoder.next_band(band.data(), band.size(), length));
    check_band_header(band, width_3x, length);
    assert(encoder.next_band(band.data(), band.size(), length));
    check_band_header(band, width_3x, length);
    assert(!encoder.next_band(band.data(), band.size(), length));
}
