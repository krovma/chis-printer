// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <atomic>

#include "types.h"
#include "gbp/protocol/packet.h"

namespace Gbp::Terminal {

struct ByteReply {
    uint8 next_tx = 0;
    bool packet_end = false;
};

// Byte-level response generator. The returned byte is shifted out during the
// next byte received from the Game Boy.
class PrinterSession {
  public:
    ByteReply feed(uint8 byte, bool buffer_full, uint32 now_us);
    void set_simulated_print_time(uint32 duration_us) { simulated_print_time_us_ = duration_us; }
    bool take_print_completion(uint32 now_us, uint32& at_us);
    void abort_receive();
    void reset();

  private:
    void apply_command(const Protocol::Header& header);
    void finish_packet(const Protocol::Header& header, uint32 now_us);
    void refresh_print_status(uint32 now_us);

    Protocol::PacketParser parser_{};
    uint8 status_ = 0;
    uint8 packet_reply_status_ = 0;
    uint32 simulated_print_time_us_ = 1'000'000;
    uint32 print_started_at_us_ = 0;
    // The IRQ serves STATUS; Core1 claims this deadline once to start AutoCut.
    std::atomic<uint32> pending_print_started_at_us_{0};
    bool packet_valid_ = false;
    bool simulated_print_active_ = false;
};

}  // namespace Gbp::Terminal
