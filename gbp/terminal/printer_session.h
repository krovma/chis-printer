// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>

#include "gbp/protocol/packet.h"

namespace gbp::terminal {

struct ByteReply {
    std::uint8_t next_tx = 0;
    bool packet_end = false;
};

// Byte-level response generator. The returned byte is shifted out during the
// next byte received from the Game Boy.
class PrinterSession {
  public:
    ByteReply feed(std::uint8_t byte, bool buffer_full);
    void reset();

  private:
    void apply_command(const protocol::Header& header);
    void finish_packet(const protocol::Header& header);

    protocol::PacketParser parser_{};
    std::uint8_t status_ = 0;
    std::uint8_t busy_countdown_ = 0;
    std::uint8_t unprocessed_countdown_ = 0;
    std::uint8_t data_countdown_ = 0;
    bool packet_valid_ = false;
};

}  // namespace gbp::terminal
