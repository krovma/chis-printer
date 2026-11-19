// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <atomic>
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
    ByteReply feed(std::uint8_t byte, bool buffer_full, std::uint32_t now_us);
    void set_simulated_print_time(std::uint32_t duration_us) { simulated_print_time_us_ = duration_us; }
    bool take_print_completion(std::uint32_t now_us, std::uint32_t& at_us);
    void reset();

  private:
    void apply_command(const protocol::Header& header);
    void finish_packet(const protocol::Header& header, std::uint32_t now_us);
    void refresh_print_status(std::uint32_t now_us);

    protocol::PacketParser parser_{};
    std::uint8_t status_ = 0;
    std::uint8_t packet_reply_status_ = 0;
    std::uint32_t simulated_print_time_us_ = 1'000'000;
    std::uint32_t print_started_at_us_ = 0;
    // The IRQ serves STATUS; Core1 claims this deadline once to start AutoCut.
    std::atomic<std::uint32_t> pending_print_started_at_us_{0};
    bool packet_valid_ = false;
    bool simulated_print_active_ = false;
};

}  // namespace gbp::terminal
