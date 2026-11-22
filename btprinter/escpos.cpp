// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "btprinter/escpos.h"

#include <algorithm>

#include "types.h"
#include "gbr/format.h"

namespace BtPrinter {
namespace {

constexpr uint8 HEADER[]{
    0x1b, 0x40,              // ESC @ (1B 40): initialize printer.
    0x1b, 0x61, 0x01,        // ESC a 1 (1B 61 01): center graphics in the printable area.
    0x1b, 0x33, 0x00,        // ESC 3 0 (1B 33 00): set line spacing to zero between bands.
};
constexpr uint8 FIRMWARE_HEADER[]{
    0x1b, 0x40,              // ESC @ (1B 40): initialize printer.
    0x1b, 0x61, 0x00,        // ESC a 0 (1B 61 00): left-align graphics.
    0x1b, 0x33, 0x00,        // ESC 3 0 (1B 33 00): set line spacing to zero between bands.
    0x1d, 0x4c, 0x00, 0x00,  // GS L nL nH (1D 4C nL nH): set left margin in horizontal units.
};
constexpr uint8 ROLL_FOOTER[]{
    0x1b, 0x32,              // ESC 2 (1B 32): restore default line spacing.
    0x1b, 0x64, 0x03,        // ESC d 3 (1B 64 03): print and feed three lines.
    0x1b, 0x61, 0x00,        // ESC a 0 (1B 61 00): restore left alignment.
    0x0a,                    // LF (0A): print and feed one line.
};
constexpr uint8 LABEL_FOOTER[]{
    0x1b, 0x32,              // ESC 2 (1B 32): restore default line spacing.
    0x1b, 0x61, 0x00,        // ESC a 0 (1B 61 00): restore left alignment.
};
// GS L 0 0 (1D 4C 00 00): restore the default left margin.
constexpr uint8 RESET_LEFT_MARGIN[]{0x1d, 0x4c, 0x00, 0x00};
constexpr usize MAX_FEED_DOTS = 255;
constexpr usize FEED_COMMAND_BYTES = 3;

template <usize N>
usize write_command(const uint8 (&command)[N], uint8* output, const usize capacity,
                    const bool reset_left_margin = false) {
    const usize length = N + (reset_left_margin ? sizeof(RESET_LEFT_MARGIN) : 0);
    if (output == nullptr || capacity < length) return 0;
    std::copy_n(command, N, output);
    if (reset_left_margin) {
        std::copy_n(RESET_LEFT_MARGIN, sizeof(RESET_LEFT_MARGIN), output + N);
    }
    return length;
}

constexpr usize feed_size(const usize dots) {
    return (dots / MAX_FEED_DOTS + (dots % MAX_FEED_DOTS != 0)) * FEED_COMMAND_BYTES;
}

usize write_feed(usize& remaining_dots, uint8* output, const usize capacity) {
    const auto advance = static_cast<uint8>(std::min(remaining_dots, MAX_FEED_DOTS));
    // ESC J n (1B 4A n): print and feed n * 0.125 mm per the vendor command set.
    const uint8 command[FEED_COMMAND_BYTES]{0x1b, 0x4a, advance};
    const usize length = write_command(command, output, capacity);
    if (length != 0) remaining_dots -= advance;
    return length;
}

}  // namespace

bool EscPosRasterEncoder::reset(const PrintBlock& block) {
    data_ = block.data;
    palette_ = Gbr::Format::preview_palette(block.palette);
    scale_quarters_ = block.scale_quarters;
    paper_ = block.paper;
    centering_ = block.centering;
    band_index_ = 0;
    encoded_size_ = 0;
    dither_workspace_.reset();
    dither_ = dither_strategy(scale_quarters_);
    const bool firmware_centering = centering_ == Config::CenteringMode::Firmware;
    const bool valid_centering = centering_ == Config::CenteringMode::Printer ||
                                 (firmware_centering && block.printable_width_dots >= output_width());
    valid_ = dither_ != nullptr && data_ != nullptr && block.size != 0 &&
             block.size % BYTES_PER_TILE_ROW == 0 && block.size / BYTES_PER_TILE_ROW <= 4095 &&
             valid_centering;
    source_height_ = valid_ ? (block.size / BYTES_PER_TILE_ROW) * 8 : 0;
    if (!valid_) return false;

    left_margin_dots_ = firmware_centering ?
        (block.left_margin_dots == Config::AUTO_LEFT_MARGIN ?
            Config::default_left_margin_dots(block.printable_width_dots, scale_quarters_) :
            block.left_margin_dots) : 0;
    header_pending_ = block.first;
    footer_pending_ = block.last;
    before_feed_remaining_ = block.before_feed_dots;
    after_feed_remaining_ = block.after_feed_dots;
    encoded_size_ = raster_size() + feed_size(before_feed_remaining_) + feed_size(after_feed_remaining_);
    if (header_pending_) {
        encoded_size_ += firmware_centering ? sizeof(FIRMWARE_HEADER) : sizeof(HEADER);
    }
    if (footer_pending_) {
        encoded_size_ += paper_ == Config::PaperType::Label ? sizeof(LABEL_FOOTER) : sizeof(ROLL_FOOTER);
        if (firmware_centering) encoded_size_ += sizeof(RESET_LEFT_MARGIN);
    }
    return true;
}

EncodeResult EscPosRasterEncoder::next_chunk(uint8* output, const usize capacity, usize& length) {
    length = 0;
    if (!valid_) return EncodeResult::Error;

    const bool firmware_centering = centering_ == Config::CenteringMode::Firmware;
    if (header_pending_) {
        if (firmware_centering) {
            length = write_command(FIRMWARE_HEADER, output, capacity);
            if (length != 0) {
                output[length - 2] = static_cast<uint8>(left_margin_dots_);
                output[length - 1] = static_cast<uint8>(left_margin_dots_ >> 8);
            }
        } else {
            length = write_command(HEADER, output, capacity);
        }
        if (length != 0) header_pending_ = false;
    } else if (before_feed_remaining_ != 0) {
        length = write_feed(before_feed_remaining_, output, capacity);
    } else if (band_index_ < band_count()) {
        return next_band(output, capacity, length) ? EncodeResult::Chunk : EncodeResult::Error;
    } else if (after_feed_remaining_ != 0) {
        length = write_feed(after_feed_remaining_, output, capacity);
    } else if (footer_pending_) {
        length = paper_ == Config::PaperType::Label ?
            write_command(LABEL_FOOTER, output, capacity, firmware_centering) :
            write_command(ROLL_FOOTER, output, capacity, firmware_centering);
        if (length != 0) footer_pending_ = false;
    } else {
        return EncodeResult::Complete;
    }
    return length != 0 ? EncodeResult::Chunk : EncodeResult::Error;
}

void EscPosRasterEncoder::dither_row(const usize output_y, uint8* bits) {
    dither_->row({data_, source_height_, palette_}, output_y, bits, dither_workspace_);
}

PrintCommandEncoder* protocol_encoder(const Config::PrintProtocol protocol,
                                      EscPosRasterEncoder& escpos) {
    return protocol == Config::PrintProtocol::EscPos ? &escpos : nullptr;
}

usize EscPosRasterEncoder::raster_size() const {
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
