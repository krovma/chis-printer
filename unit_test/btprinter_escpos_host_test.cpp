// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <algorithm>
#include <array>
#include <cassert>
#include <initializer_list>

#include "types.h"
#include "btprinter/escpos.h"
#include "btprinter/tspl.h"
#include "btprinter/print_layout.h"

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
    using BtPrinter::EncodeResult;
    constexpr usize width_2x = BtPrinter::SOURCE_WIDTH * 2;
    constexpr usize width_3x = BtPrinter::SOURCE_WIDTH * 3;
    constexpr usize width_5x = BtPrinter::SOURCE_WIDTH * 5;
    std::array<uint8, BtPrinter::BYTES_PER_TILE_ROW> image{};
    std::array<uint8, BtPrinter::MAX_BAND_BYTES> band{};
    BtPrinter::EscPosRasterEncoder encoder;
    usize length = 0;

    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_2X}));
    assert(encoder.output_width() == width_2x);
    assert(encoder.band_count() == 1);
    assert(encoder.encoded_size() == 8 + 16 * 40);
    assert(encoder.next_chunk(band.data(), encoder.encoded_size() - 1, length) == EncodeResult::Error);
    assert(encoder.next_chunk(band.data(), encoder.encoded_size(), length) == EncodeResult::Chunk);
    check_raster_header(band, width_2x, 16, length);
    assert(black_dots(band, length) == 0);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);

    image.fill(0xff);
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_2X}));
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, width_2x, 16, length);
    assert(black_dots(band, length) == width_2x * 16);
    for (usize offset = BtPrinter::RASTER_HEADER_BYTES; offset < length; ++offset) {
        assert(band[offset] == 0xff);
    }

    image.fill(0);
    image[0] = image[1] = 0x80;  // Top-left source pixel.
    image[318] = image[319] = 0x01;  // Bottom-right source pixel.
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_2X}));
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    assert(black_dots(band, length) == 8);
    assert(band[8] == 0xc0 && band[8 + 40] == 0xc0);
    assert(band[8 + 14 * 40 + 39] == 0x03);
    assert(band[8 + 15 * 40 + 39] == 0x03);

    image.fill(0);
    for (usize i = 0; i < image.size(); i += 2) {
        image[i] = 0xff;
    }
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_2X}));
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    const usize gray_dots = black_dots(band, length);
    assert(gray_dots > 0 && gray_dots < width_2x * 16);

    std::array<uint8, BtPrinter::BYTES_PER_TILE_ROW * 2> tall_image{};
    tall_image.fill(0xff);
    assert(encoder.reset({tall_image.data(), tall_image.size(), 0xE4, Config::SCALE_2X}));
    assert(encoder.band_count() == 2);
    assert(encoder.encoded_size() == 5 + width_2x * 3 + 1 + 8 + 8 * 40);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_band_header(band, width_2x, length);
    assert(black_dots(band, length) == width_2x * 24);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, width_2x, 8, length);
    assert(black_dots(band, length) == width_2x * 8);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);

    image.fill(0);
    for (usize i = 0; i < image.size(); i += 2) image[i] = 0xff;
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_1X}));
    assert(encoder.output_width() == BtPrinter::SOURCE_WIDTH);
    assert(encoder.output_height() == 8);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, BtPrinter::SOURCE_WIDTH, 8, length);
    assert(black_dots(band, length) == 400); // Bayer thresholds for shade 1.
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
    assert(!encoder.reset({image.data(), image.size(), 0xE4, 7}));

    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_1_5X}));
    assert(encoder.output_width() == 240 && encoder.output_height() == 12);
    assert(encoder.encoded_size() == 8 + 12 * 30);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, 240, 12, length);
    assert(black_dots(band, length) > 0 && black_dots(band, length) < 2880);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);

    image.fill(0);
    image[0] = image[1] = 0x80;
    image[318] = image[319] = 0x01;
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_1_5X}));
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, 240, 12, length);
    assert(black_dots(band, length) == 5);
    assert(band[8] == 0xc0 && band[8 + 30] == 0xc0);
    assert(band[8 + 11 * 30 + 29] == 0x01);

    std::array<uint8, BtPrinter::BYTES_PER_TILE_ROW * 3> fractional_image{};
    fractional_image.fill(0xff);
    assert(encoder.reset({fractional_image.data(), fractional_image.size(), 0xE4,
                          Config::SCALE_1_5X}));
    assert(encoder.output_height() == 36 && encoder.band_count() == 2);
    assert(encoder.encoded_size() == 5 + 240 * 3 + 1 + 8 + 12 * 30);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_band_header(band, 240, length);
    assert(black_dots(band, length) == 240 * 24);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, 240, 12, length);
    assert(black_dots(band, length) == 240 * 12);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);

    image.fill(0xff);
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_4X}));
    assert(encoder.output_width() == BtPrinter::SOURCE_WIDTH * 4);
    assert(encoder.output_height() == 32);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_band_header(band, BtPrinter::SOURCE_WIDTH * 4, length);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, BtPrinter::SOURCE_WIDTH * 4, 8, length);

    image.fill(0xff);
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_3X}));
    assert(encoder.output_width() == width_3x);
    assert(encoder.output_height() == 24);
    assert(encoder.band_count() == 1);
    assert(encoder.encoded_size() == 5 + width_3x * 3 + 1);
    assert(encoder.next_chunk(band.data(), encoder.encoded_size() - 1, length) == EncodeResult::Error);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_band_header(band, width_3x, length);
    assert(black_dots(band, length) == width_3x * 24);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);

    image.fill(0);
    image[0] = image[1] = 0x80;
    image[318] = image[319] = 0x01;
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_3X}));
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    assert(black_dots(band, length) == 18);
    assert(band[5] == 0xe0 && band[5 + 3] == 0xe0 && band[5 + 6] == 0xe0);
    assert(band[5 + 477 * 3 + 2] == 0x07 && band[5 + 479 * 3 + 2] == 0x07);

    tall_image.fill(0xff);
    assert(encoder.reset({tall_image.data(), tall_image.size(), 0xE4, Config::SCALE_3X}));
    assert(encoder.band_count() == 2);
    assert(encoder.encoded_size() == 2 * (5 + width_3x * 3 + 1));
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_band_header(band, width_3x, length);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_band_header(band, width_3x, length);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);

    image.fill(0xff);
    assert(encoder.reset({image.data(), image.size(), 0xE4, Config::SCALE_5X}));
    assert(encoder.output_width() == width_5x && encoder.output_height() == 40);
    assert(encoder.band_count() == 2);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_band_header(band, width_5x, length);
    assert(length == BtPrinter::MAX_BAND_BYTES);
    assert(encoder.next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
    check_raster_header(band, width_5x, 16, length);
    assert(black_dots(band, length) == width_5x * 16);

    BtPrinter::TsplRasterEncoder tspl;
    auto* stream = BtPrinter::protocol_encoder(Config::PrintProtocol::EscPos, encoder, tspl);
    assert(stream == &encoder);
    assert(BtPrinter::protocol_encoder(Config::PrintProtocol::Tspl, encoder, tspl) == &tspl);
    assert(BtPrinter::protocol_encoder(static_cast<Config::PrintProtocol>(2), encoder, tspl) == nullptr);
    usize sent = 0;
    const auto read_chunk = [&] {
        assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Chunk);
        sent += length;
    };
    const auto expect_command = [&](const std::initializer_list<uint8> expected) {
        read_chunk();
        assert(length == expected.size());
        assert(std::equal(expected.begin(), expected.end(), band.begin()));
    };
    assert(stream->reset({image.data(), image.size(), 0xE4, Config::SCALE_1X,
                          true, true, Config::PaperType::Label, 400, 256}));
    expect_command({0x1b, 0x40, 0x1b, 0x61, 0x01, 0x1b, 0x33, 0x00});
    expect_command({0x1b, 0x4a, 255});
    expect_command({0x1b, 0x4a, 145});
    read_chunk();
    check_raster_header(band, BtPrinter::SOURCE_WIDTH, 8, length);
    expect_command({0x1b, 0x4a, 255});
    expect_command({0x1b, 0x4a, 1});
    expect_command({0x1b, 0x32, 0x1b, 0x61, 0x00});
    assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
    assert(sent == stream->encoded_size());

    sent = 0;
    assert(stream->reset({image.data(), image.size(), 0xE4, Config::SCALE_2X,
                          false, true, Config::PaperType::Roll, 16, 24}));
    expect_command({0x1b, 0x4a, 16});
    read_chunk();
    check_raster_header(band, width_2x, 16, length);
    expect_command({0x1b, 0x4a, 24});
    expect_command({0x1b, 0x32, 0x1b, 0x64, 0x03, 0x1b, 0x61, 0x00, 0x0a});
    assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
    assert(sent == stream->encoded_size());

    sent = 0;
    assert(stream->reset({image.data(), image.size(), 0xE4, Config::SCALE_2X, false, false,
                          Config::PaperType::Roll, 8, 0}));
    expect_command({0x1b, 0x4a, 8});
    read_chunk();
    check_raster_header(band, width_2x, 16, length);
    assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
    assert(sent == stream->encoded_size());

    constexpr auto firmware = Config::CenteringMode::Firmware;
    constexpr uint16 wide_printable = 831;
    for (const bool first : {true, false}) {
        sent = 0;
        assert(stream->reset({image.data(), image.size(), 0xE4, Config::SCALE_5X, first, !first,
                              Config::PaperType::Roll, 0, 0, wide_printable, firmware}));
        if (first) {
            expect_command({0x1b, 0x40, 0x1b, 0x61, 0x00, 0x1b, 0x33, 0x00, 0x1d, 0x4c, 0x0f, 0x00});
        }
        read_chunk();
        check_band_header(band, width_5x, length);
        read_chunk();
        check_raster_header(band, width_5x, 16, length);
        if (!first) {
            expect_command({0x1b, 0x32, 0x1b, 0x64, 0x03, 0x1b, 0x61, 0x00, 0x0a, 0x1d, 0x4c, 0x00, 0x00});
        }
        assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
        assert(sent == stream->encoded_size());
    }

    sent = 0;
    assert(stream->reset({image.data(), image.size(), 0xE4, Config::SCALE_1X, true, true,
                          Config::PaperType::Label, 0, 0, wide_printable, firmware}));
    expect_command({0x1b, 0x40, 0x1b, 0x61, 0x00, 0x1b, 0x33, 0x00, 0x1d, 0x4c, 0x4f, 0x01});
    read_chunk();
    check_raster_header(band, BtPrinter::SOURCE_WIDTH, 8, length);
    expect_command({0x1b, 0x32, 0x1b, 0x61, 0x00, 0x1d, 0x4c, 0x00, 0x00});
    assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
    assert(sent == stream->encoded_size());

    struct MarginCase { uint8 scale; uint16 printable_width; uint16 expected_margin; };
    constexpr MarginCase defaults[]{
        {Config::SCALE_1X, 280, 60},
        {Config::SCALE_1_5X, 280, 20},
        {Config::SCALE_1_5X, 384, 72},
    };
    for (const auto& profile : defaults) {
        sent = 0;
        assert(stream->reset({image.data(), image.size(), 0xE4, profile.scale,
                              true, false, Config::PaperType::Roll, 0, 0,
                              profile.printable_width, firmware}));
        expect_command({0x1b, 0x40, 0x1b, 0x61, 0x00, 0x1b, 0x33, 0x00,
                        0x1d, 0x4c, static_cast<uint8>(profile.expected_margin),
                        static_cast<uint8>(profile.expected_margin >> 8)});
        read_chunk();
        check_raster_header(band, Config::scaled_dots(BtPrinter::SOURCE_WIDTH, profile.scale),
                            Config::scaled_dots(8, profile.scale), length);
        assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
        assert(sent == stream->encoded_size());
    }

    sent = 0;
    assert(stream->reset({image.data(), image.size(), 0xE4, Config::SCALE_1_5X,
                          true, true, Config::PaperType::Roll, 0, 0, 240, firmware, 42}));
    expect_command({0x1b, 0x40, 0x1b, 0x61, 0x00, 0x1b, 0x33, 0x00, 0x1d, 0x4c, 0x2a, 0x00});
    read_chunk();
    check_raster_header(band, 240, 12, length);
    expect_command({0x1b, 0x32, 0x1b, 0x64, 0x03, 0x1b, 0x61, 0x00, 0x0a, 0x1d, 0x4c, 0x00, 0x00});
    assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
    assert(sent == stream->encoded_size());

    sent = 0;
    assert(stream->reset({image.data(), image.size(), 0xE4, Config::SCALE_5X,
                          true, false, Config::PaperType::Roll, 0, 0, wide_printable, firmware, 22}));
    expect_command({0x1b, 0x40, 0x1b, 0x61, 0x00, 0x1b, 0x33, 0x00, 0x1d, 0x4c, 0x16, 0x00});
    read_chunk();
    check_band_header(band, width_5x, length);
    read_chunk();
    check_raster_header(band, width_5x, 16, length);
    assert(stream->next_chunk(band.data(), band.size(), length) == EncodeResult::Complete);
    assert(sent == stream->encoded_size());

    const auto centered = BtPrinter::layout_label(16, 56, Config::SCALE_2X, 50);
    assert(centered.label_dots == 400 && centered.content_dots == 144);
    assert(centered.top_dots == 128 && centered.bottom_dots == 128 && !centered.overflow);
    const auto overflow = BtPrinter::layout_label(16, 56, Config::SCALE_2X, 10);
    assert(overflow.overflow && overflow.top_dots == 0 && overflow.bottom_dots == 0);
    const auto original = BtPrinter::layout_label(16, 56, Config::SCALE_1X, 10);
    assert(!original.overflow && original.content_dots == 72);
    assert(original.top_dots + original.content_dots + original.bottom_dots == original.label_dots);
    const auto fractional = BtPrinter::layout_label(16, 56, Config::SCALE_1_5X, 10);
    assert(fractional.content_dots == 108 && fractional.label_dots == 80 && fractional.overflow);
    assert(BtPrinter::millimeters_to_dots(2) == 16);
}
