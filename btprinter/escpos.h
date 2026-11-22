// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"
#include "btprinter/dither.h"
#include "btprinter/protocol.h"

namespace BtPrinter {

inline constexpr usize DOT_BAND_HEIGHT = 24;
inline constexpr usize RASTER_HEADER_BYTES = 8;
// Maximum size: a full-height ESC * band followed by LF.
inline constexpr usize MAX_BAND_BYTES = 5 + MAX_OUTPUT_WIDTH * 3 + 1;
static_assert(MAX_OUTPUT_WIDTH <= 1023 && MAX_OUTPUT_WIDTH % 8 == 0);

// Streams one dithered ESC/POS band at a time without storing a scaled image.
class EscPosRasterEncoder final : public PrintCommandEncoder {
  public:
    bool reset(const PrintBlock& block) override;
    EncodeResult next_chunk(uint8* output, usize capacity, usize& length) override;
    usize encoded_size() const override { return encoded_size_; }
    usize output_width() const { return Config::scaled_dots(SOURCE_WIDTH, scale_quarters_); }
    usize output_height() const { return Config::scaled_dots(source_height_, scale_quarters_); }
    usize band_count() const { return (output_height() + DOT_BAND_HEIGHT - 1) / DOT_BAND_HEIGHT; }

  private:
    usize raster_row_bytes() const { return output_width() / 8; }
    usize band_bytes() const { return 5 + output_width() * 3 + 1; }
    usize raster_size() const;
    bool next_band(uint8* output, usize capacity, usize& length);
    void dither_row(usize output_y, uint8* bits);

    const uint8* data_ = nullptr;
    usize source_height_ = 0;
    uint8 palette_ = 0;
    uint8 scale_quarters_ = DEFAULT_IMAGE_SCALE_QUARTERS;
    usize band_index_ = 0;
    usize encoded_size_ = 0;
    usize before_feed_remaining_ = 0;
    usize after_feed_remaining_ = 0;
    Config::PaperType paper_ = Config::PaperType::Roll;
    Config::CenteringMode centering_ = Config::CenteringMode::Printer;
    uint16 left_margin_dots_ = 0;
    bool header_pending_ = false;
    bool footer_pending_ = false;
    bool valid_ = false;
    const DitherStrategy* dither_ = nullptr;
    DitherWorkspace dither_workspace_{};
    std::array<uint8, MAX_OUTPUT_WIDTH> row_bits_{};
};

PrintCommandEncoder* protocol_encoder(Config::PrintProtocol protocol,
                                      EscPosRasterEncoder& escpos);

}  // namespace BtPrinter
