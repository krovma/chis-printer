// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>

#include "btprinter/print_layout.h"
#include "btprinter/tspl.h"

namespace {

BtPrinter::PrintBlock make_block(const std::array<uint8, BtPrinter::BYTES_PER_TILE_ROW>& image) {
    BtPrinter::PrintBlock block{};
    block.data = image.data();
    block.size = image.size();
    block.palette = 0xE4;
    block.scale_quarters = Config::SCALE_2X;
    block.paper = Config::PaperType::Label;
    block.printable_width_dots = 384;
    block.centering = Config::CenteringMode::Printer;
    block.left_margin_dots = 72;
    block.media_width_mm = 58;
    block.label_length_mm = 50;
    block.label_gap_mm = 2;
    block.print_direction = 0;
    return block;
}

std::string drain(BtPrinter::TsplRasterEncoder& encoder) {
    std::array<uint8, 2048> chunk{};
    std::string output;
    for (usize call = 0; call < 100; ++call) {
        usize length = 0;
        const auto result = encoder.next_chunk(chunk.data(), chunk.size(), length);
        if (result == BtPrinter::EncodeResult::Complete) {
            assert(length == 0);
            return output;
        }
        assert(result == BtPrinter::EncodeResult::Chunk);
        assert(length > 0 && length <= chunk.size());
        output.append(reinterpret_cast<const char*>(chunk.data()), length);
    }
    assert(false);
    return {};
}

void check_raster(const std::string& output, const std::string_view command,
                  const usize width_bytes, const usize height, const uint8 expected) {
    const usize command_start = output.find(command);
    assert(command_start != std::string::npos);
    const usize raster_start = command_start + command.size();
    for (usize row = 0; row < height; ++row) {
        for (usize byte = 0; byte < width_bytes; ++byte) {
            assert(static_cast<uint8>(output[raster_start + row * width_bytes + byte]) == expected);
        }
    }
}

}  // namespace

int main() {
    std::array<uint8, BtPrinter::BYTES_PER_TILE_ROW> image{};
    image.fill(0xff);
    BtPrinter::TsplRasterEncoder encoder;

    auto first = make_block(image);
    first.first = true;
    assert(encoder.reset(first));
    constexpr std::string_view label_setup{
        "SIZE 58 mm,50 mm\r\nGAP 2 mm,0 mm\r\nDIRECTION 0\r\nCLS\r\n"};
    assert(encoder.encoded_size() == label_setup.size() + 20 + 40 * 16);
    const std::string first_output = drain(encoder);
    assert(std::string_view{first_output}.substr(0, label_setup.size()) == label_setup);
    assert(first_output.find("PRINT") == std::string::npos);
    check_raster(first_output, "BITMAP 72,0,40,16,0,", 40, 16, 0x00);
    assert(first_output.size() == encoder.encoded_size());

    image.fill(0);
    auto last = make_block(image);
    last.last = true;
    last.before_feed_dots = 8;
    assert(encoder.reset(last));
    assert(encoder.encoded_size() == 21 + 40 * 16 + 13);
    const std::string last_output = drain(encoder);
    constexpr std::string_view last_bitmap{"BITMAP 72,24,40,16,0,"};
    constexpr std::string_view print_command{"\r\nPRINT 1,1\r\n"};
    assert(std::string_view{last_output}.substr(0, last_bitmap.size()) == last_bitmap);
    assert(std::string_view{last_output}.substr(last_output.size() - print_command.size()) == print_command);
    check_raster(last_output, "BITMAP 72,24,40,16,0,", 40, 16, 0xff);
    assert(last_output.size() == encoder.encoded_size());

    image.fill(0);
    auto white = make_block(image);
    white.first = true;
    white.last = true;
    assert(encoder.reset(white));
    const std::string white_output = drain(encoder);
    check_raster(white_output, "BITMAP 72,0,40,16,0,", 40, 16, 0xff);

    auto normal_direction = make_block(image);
    normal_direction.first = true;
    normal_direction.last = true;
    normal_direction.print_direction = 1;
    assert(encoder.reset(normal_direction));
    const std::string normal_output = drain(encoder);
    constexpr std::string_view normal_setup{
        "SIZE 58 mm,50 mm\r\nGAP 2 mm,0 mm\r\nDIRECTION 1\r\nCLS\r\n"};
    assert(std::string_view{normal_output}.substr(0, normal_setup.size()) == normal_setup);
    check_raster(normal_output, "BITMAP 72,0,40,16,0,", 40, 16, 0xff);

    auto firmware_margin = make_block(image);
    firmware_margin.first = true;
    firmware_margin.last = true;
    firmware_margin.print_direction = 0;
    firmware_margin.centering = Config::CenteringMode::Firmware;
    firmware_margin.left_margin_dots = 16;
    assert(encoder.reset(firmware_margin));
    const std::string firmware_margin_output = drain(encoder);
    check_raster(firmware_margin_output, "BITMAP 56,0,40,16,0,", 40, 16, 0xff);

    auto roll = make_block(image);
    roll.paper = Config::PaperType::Roll;
    roll.first = true;
    roll.last = true;
    roll.label_gap_mm = 50;
    uint16 roll_length_mm = 0;
    assert(BtPrinter::tspl_roll_label_length_mm(8, Config::SCALE_2X, roll_length_mm));
    assert(roll_length_mm == 17);
    roll.label_length_mm = roll_length_mm;
    assert(encoder.reset(roll));
    const std::string roll_output = drain(encoder);
    constexpr std::string_view roll_setup{
        "SIZE 58 mm,17 mm\r\nGAP 0 mm,0 mm\r\nDIRECTION 0\r\nCLS\r\n"};
    assert(std::string_view{roll_output}.substr(0, roll_setup.size()) == roll_setup);
    check_raster(roll_output, "BITMAP 72,0,40,16,0,", 40, 16, 0xff);

    assert(BtPrinter::tspl_roll_label_length_mm(144, Config::SCALE_2X, roll_length_mm));
    assert(roll_length_mm == 51);
    assert(!BtPrinter::tspl_roll_label_length_mm(5000, Config::SCALE_2X, roll_length_mm));

    last.label_gap_mm = Config::MAX_TSPL_LABEL_GAP_MM + 1;
    assert(!encoder.reset(last));
    last.label_gap_mm = 2;
    last.media_width_mm = 37;
    assert(!encoder.reset(last));
    last.media_width_mm = 58;
    last.paper = Config::PaperType::Roll;
    last.label_gap_mm = 0;
    last.label_length_mm = 76;
    assert(encoder.reset(last));
    last.paper = Config::PaperType::Label;
    last.print_direction = 2;
    assert(!encoder.reset(last));

    const auto layout = BtPrinter::layout_label(16, 0, Config::SCALE_2X, 50);
    assert(layout.label_dots == 400 && layout.content_dots == 32);
    assert(layout.top_dots == 184 && layout.bottom_dots == 184);
    assert(BtPrinter::layout_label(201, 0, Config::SCALE_2X, 50).overflow);
    assert(BtPrinter::layout_label(201, 0, Config::SCALE_2X, 50).content_dots > 400);
}
