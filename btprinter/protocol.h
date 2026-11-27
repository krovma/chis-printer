// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"
#include "config/printer_config.h"

namespace BtPrinter {

struct PrintBlock {
    const uint8* data;
    usize size;
    uint8 palette;
    uint8 scale_quarters;
    bool first = false;
    bool last = false;
    Config::PaperType paper = Config::PaperType::Roll;
    usize before_feed_dots = 0;
    usize after_feed_dots = 0;
    uint16 printable_width_dots = 0;
    Config::CenteringMode centering = Config::CenteringMode::Printer;
    uint16 left_margin_dots = Config::AUTO_LEFT_MARGIN;
    uint16 media_width_mm = 0;
    uint16 label_length_mm = Config::DEFAULT_LABEL_LENGTH_MM;
    uint16 label_gap_mm = 0;
    uint8 print_direction = 1;
};

enum class EncodeResult : uint8 { Chunk, Complete, Error };

// Command encoding is independent of the RFCOMM transport so ESC/POS and TSPL
// use the same send loop.
class PrintCommandEncoder {
  public:
    virtual ~PrintCommandEncoder() = default;
    virtual bool reset(const PrintBlock& block) = 0;
    virtual EncodeResult next_chunk(uint8* output, usize capacity, usize& length) = 0;
    virtual usize encoded_size() const = 0;
};

}  // namespace BtPrinter
