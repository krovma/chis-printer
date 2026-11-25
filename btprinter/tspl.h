// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"
#include "btprinter/dither.h"
#include "btprinter/protocol.h"

namespace BtPrinter {

class TsplRasterEncoder final : public PrintCommandEncoder {
  public:
    bool reset(const PrintBlock& block) override;
    EncodeResult next_chunk(uint8* output, usize capacity, usize& length) override;
    usize encoded_size() const override { return encoded_size_; }

  private:
    enum class Stage : uint8 { Header, BitmapHeader, Raster, Footer, Complete };

    const uint8* data_ = nullptr;
    usize source_height_ = 0;
    usize image_width_ = 0;
    usize bitmap_y_ = 0;
    usize row_bytes_ = 0;
    usize row_index_ = 0;
    usize encoded_size_ = 0;
    usize footer_offset_ = 0;
    usize header_length_ = 0;
    usize header_offset_ = 0;
    usize bitmap_header_length_ = 0;
    usize bitmap_header_offset_ = 0;
    uint8 palette_ = 0;
    uint8 scale_quarters_ = DEFAULT_IMAGE_SCALE_QUARTERS;
    std::array<char, 128> header_{};
    std::array<char, 64> bitmap_header_{};
    DitherWorkspace dither_workspace_{};
    std::array<uint8, MAX_OUTPUT_WIDTH> row_bits_{};
    const DitherStrategy* dither_ = nullptr;
    bool last_block_ = false;
    bool valid_ = false;
    Stage stage_ = Stage::Complete;
};

}  // namespace BtPrinter
