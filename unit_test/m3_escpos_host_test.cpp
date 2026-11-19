// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "m3/escpos.h"

namespace {

std::size_t black_dots(const std::array<std::uint8_t, m3::BAND_BYTES>& band,
                       const std::size_t length) {
    std::size_t count = 0;
    const std::size_t first = band[0] == 0x1d ? m3::RASTER_HEADER_BYTES : 5;
    const std::size_t last = band[0] == 0x1d ? length : length - 1;
    for (std::size_t offset = first; offset < last; ++offset) {
        for (std::uint8_t bit = 0; bit < 8; ++bit) count += (band[offset] >> bit) & 1u;
    }
    return count;
}

void check_band_header(const std::array<std::uint8_t, m3::BAND_BYTES>& band) {
    assert(band[0] == 0x1b && band[1] == 0x2a && band[2] == 33);
    assert(band[3] == 0x40 && band[4] == 0x01);  // 320 dots.
    assert(band.back() == '\n');
}

void check_raster_header(const std::array<std::uint8_t, m3::BAND_BYTES>& band,
                         const std::size_t rows, const std::size_t length) {
    assert(band[0] == 0x1d && band[1] == 0x76 && band[2] == 0x30 && band[3] == 0);
    assert(band[4] == m3::RASTER_ROW_BYTES && band[5] == 0);
    assert(band[6] == rows && band[7] == 0);
    assert(length == m3::RASTER_HEADER_BYTES + rows * m3::RASTER_ROW_BYTES);
}

}  // namespace

int main() {
    std::array<std::uint8_t, m3::BYTES_PER_TILE_ROW> image{};
    std::array<std::uint8_t, m3::BAND_BYTES> band{};
    m3::EscPosRasterEncoder encoder;
    std::size_t length = 0;

    assert(encoder.reset(image.data(), image.size(), 0xE4));
    assert(encoder.band_count() == 1);
    assert(encoder.encoded_size() == 8 + 16 * 40);
    assert(encoder.next_band(band.data(), band.size(), length));
    check_raster_header(band, 16, length);
    assert(black_dots(band, length) == 0);
    assert(!encoder.next_band(band.data(), band.size(), length));

    image.fill(0xff);
    assert(encoder.reset(image.data(), image.size(), 0xE4));
    assert(encoder.next_band(band.data(), band.size(), length));
    check_raster_header(band, 16, length);
    assert(black_dots(band, length) == m3::OUTPUT_WIDTH * 16);
    for (std::size_t offset = m3::RASTER_HEADER_BYTES; offset < length; ++offset) {
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
    for (std::size_t i = 0; i < image.size(); i += 2) image[i] = 0xff;
    assert(encoder.reset(image.data(), image.size(), 0xE4));
    assert(encoder.next_band(band.data(), band.size(), length));
    const std::size_t gray_dots = black_dots(band, length);
    assert(gray_dots > 0 && gray_dots < m3::OUTPUT_WIDTH * 16);

    std::array<std::uint8_t, m3::BYTES_PER_TILE_ROW * 2> tall_image{};
    tall_image.fill(0xff);
    assert(encoder.reset(tall_image.data(), tall_image.size(), 0xE4));
    assert(encoder.band_count() == 2);
    assert(encoder.encoded_size() == m3::BAND_BYTES + 8 + 8 * 40);
    assert(encoder.next_band(band.data(), band.size(), length));
    assert(length == m3::BAND_BYTES);
    check_band_header(band);
    assert(black_dots(band, length) == m3::OUTPUT_WIDTH * 24);
    assert(encoder.next_band(band.data(), band.size(), length));
    check_raster_header(band, 8, length);
    assert(black_dots(band, length) == m3::OUTPUT_WIDTH * 8);
    assert(!encoder.next_band(band.data(), band.size(), length));
}
