// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "btprinter/tspl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "gbr/format.h"
#include "btprinter/print_layout.h"

namespace BtPrinter {
namespace {

constexpr char FOOTER[] = "\r\nPRINT 1,1\r\n";

}  // namespace

bool TsplRasterEncoder::reset(const PrintBlock& block) {
    valid_ = false;
    encoded_size_ = 0;
    header_length_ = 0;
    header_offset_ = 0;
    bitmap_header_length_ = 0;
    bitmap_header_offset_ = 0;
    row_index_ = 0;
    footer_offset_ = 0;

    const auto* strategy = dither_strategy(block.scale_quarters);
    const usize width = Config::scaled_dots(SOURCE_WIDTH, block.scale_quarters);
    const uint16 gap_mm = block.paper == Config::PaperType::Roll ? 0 : block.label_gap_mm;
    if (block.data == nullptr || block.size == 0 || block.size % BYTES_PER_TILE_ROW != 0 ||
        block.size / BYTES_PER_TILE_ROW > 4095 || strategy == nullptr || width == 0 ||
        width > MAX_OUTPUT_WIDTH || width % 8 != 0 ||
        (block.paper != Config::PaperType::Label && block.paper != Config::PaperType::Roll) ||
        block.print_direction > 1 ||
        block.media_width_mm == 0 || block.label_length_mm == 0 ||
        block.label_length_mm > Config::MAX_LABEL_LENGTH_MM ||
        (block.paper == Config::PaperType::Label && gap_mm > Config::MAX_TSPL_LABEL_GAP_MM) ||
        block.printable_width_dots == 0) {
        return false;
    }

    const usize media_width = millimeters_to_dots(block.media_width_mm);
    if (block.printable_width_dots > media_width ||
        width > block.printable_width_dots) {
        return false;
    }
    const usize head_offset = (media_width - block.printable_width_dots) / 2;
    const usize image_offset = block.centering == Config::CenteringMode::Firmware ?
        block.left_margin_dots :
        Config::default_left_margin_dots(block.printable_width_dots, block.scale_quarters);
    const usize x = head_offset + image_offset;
    if (x + width > media_width) {
        return false;
    }

    const usize source_height = (block.size / BYTES_PER_TILE_ROW) * 8u;
    const usize image_height = Config::scaled_dots(source_height, block.scale_quarters);
    const usize label_height = millimeters_to_dots(block.label_length_mm);
    const usize start_y = block.first ? block.before_feed_dots : bitmap_y_ + block.before_feed_dots;
    if (start_y > label_height || image_height > label_height - start_y) {
        return false;
    }
    const usize bitmap_x = x;
    // Keep DIRECTION 0 blocks in send order instead of mirroring their Y coordinates.
    const usize bitmap_y = start_y;

    int header_length = 0;
    if (block.first) {
        header_length = std::snprintf(header_.data(), header_.size(),
                                      "SIZE %u mm,%u mm\r\nGAP %u mm,0 mm\r\nDIRECTION %u\r\nCLS\r\n",
                                      static_cast<unsigned>(block.media_width_mm),
                                      static_cast<unsigned>(block.label_length_mm),
                                      static_cast<unsigned>(gap_mm),
                                      static_cast<unsigned>(block.print_direction));
    }
    const int bitmap_header_length = std::snprintf(
        bitmap_header_.data(), bitmap_header_.size(), "BITMAP %lu,%lu,%lu,%lu,0,",
        static_cast<unsigned long>(bitmap_x), static_cast<unsigned long>(bitmap_y),
        static_cast<unsigned long>(width / 8u), static_cast<unsigned long>(image_height));
    if ((block.first && (header_length <= 0 || static_cast<usize>(header_length) >= header_.size())) ||
        bitmap_header_length <= 0 ||
        static_cast<usize>(bitmap_header_length) >= bitmap_header_.size()) {
        return false;
    }

    data_ = block.data;
    source_height_ = source_height;
    image_width_ = width;
    bitmap_y_ = start_y + image_height;
    row_bytes_ = width / 8u;
    palette_ = Gbr::Format::preview_palette(block.palette);
    scale_quarters_ = block.scale_quarters;
    dither_ = strategy;
    last_block_ = block.last;
    dither_workspace_.reset();
    header_length_ = static_cast<usize>(std::max(header_length, 0));
    bitmap_header_length_ = static_cast<usize>(bitmap_header_length);
    encoded_size_ = header_length_ + bitmap_header_length_ + row_bytes_ * image_height;
    if (last_block_) {
        encoded_size_ += sizeof(FOOTER) - 1;
    }
    stage_ = block.first ? Stage::Header : Stage::BitmapHeader;
    valid_ = true;
    return true;
}

EncodeResult TsplRasterEncoder::next_chunk(uint8* output, const usize capacity, usize& length) {
    length = 0;
    if (!valid_ || output == nullptr || capacity == 0) {
        return EncodeResult::Error;
    }

    switch (stage_) {
        case Stage::Header: {
            length = std::min(capacity, header_length_ - header_offset_);
            std::memcpy(output, header_.data() + header_offset_, length);
            header_offset_ += length;
            if (header_offset_ == header_length_) stage_ = Stage::BitmapHeader;
            break;
        }
        case Stage::BitmapHeader: {
            length = std::min(capacity, bitmap_header_length_ - bitmap_header_offset_);
            std::memcpy(output, bitmap_header_.data() + bitmap_header_offset_, length);
            bitmap_header_offset_ += length;
            if (bitmap_header_offset_ == bitmap_header_length_) stage_ = Stage::Raster;
            break;
        }
        case Stage::Raster: {
            const usize raster_height = Config::scaled_dots(source_height_, scale_quarters_);
            const usize remaining_rows = raster_height - row_index_;
            const usize rows_per_chunk = std::min(remaining_rows, capacity / row_bytes_);
            if (rows_per_chunk == 0) {
                return EncodeResult::Error;
            }
            for (usize row = 0; row < rows_per_chunk; ++row) {
                uint8* raster_row = output + row * row_bytes_;
                // Gprinter BITMAP mode 0 renders zero bits as black.
                std::fill_n(raster_row, row_bytes_, 0xff);
                dither_->row({data_, source_height_, palette_}, row_index_, row_bits_.data(),
                             dither_workspace_);
                for (usize x = 0; x < image_width_; ++x) {
                    if (row_bits_[x] != 0) {
                        const uint8 mask = static_cast<uint8>(1u << (7u - x % 8u));
                        raster_row[x / 8u] = static_cast<uint8>(raster_row[x / 8u] & ~mask);
                    }
                }
                ++row_index_;
            }
            length = rows_per_chunk * row_bytes_;
            if (row_index_ == raster_height) stage_ = last_block_ ? Stage::Footer : Stage::Complete;
            break;
        }
        case Stage::Footer: {
            const usize remaining = sizeof(FOOTER) - 1 - footer_offset_;
            length = std::min(capacity, remaining);
            std::memcpy(output, FOOTER + footer_offset_, length);
            footer_offset_ += length;
            if (footer_offset_ == sizeof(FOOTER) - 1) stage_ = Stage::Complete;
            break;
        }
        case Stage::Complete:
            return EncodeResult::Complete;
    }
    return EncodeResult::Chunk;
}

}  // namespace BtPrinter
