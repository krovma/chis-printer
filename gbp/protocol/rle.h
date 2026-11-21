// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"

namespace Gbp::Protocol {

// Game Boy Printer DATA compression: literal runs of 1..128 bytes and
// repeated runs of 2..129 bytes. State is reset for each DATA packet.
class RleDecoder {
  public:
    void reset(bool compressed);
    usize feed(uint8 input, uint8 output[129]);
    bool complete() const;

  private:
    enum class State : uint8 { Control, Literal, RepeatValue };
    State state_ = State::Control;
    uint8 remaining_ = 0;
    bool compressed_ = false;
};

}  // namespace Gbp::Protocol
