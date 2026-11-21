// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"

namespace Gbp::Protocol {

inline constexpr uint8 DEVICE_ID = 0x81;
inline constexpr uint8 STATUS_UNPROCESSED = 0x08;
inline constexpr uint8 STATUS_FULL = 0x04;
inline constexpr uint8 STATUS_BUSY = 0x02;
inline constexpr uint8 STATUS_CHECKSUM_ERROR = 0x01;
inline constexpr uint8 STATUS_ERROR_MASK = 0xF1;

enum class Command : uint8 {
    Init = 0x01,
    Print = 0x02,
    Data = 0x04,
    Break = 0x08,
    Inquiry = 0x0F,
};

struct Header {
    uint8 command = 0;
    uint8 compression = 0;
    uint16 length = 0;
};

enum class Event : uint8 {
    None,
    Sync,
    PacketStart,
    PayloadByte,
    PacketComplete,
    DummyId,
    DummyStatus,
};

struct ParseResult {
    Event event = Event::None;
    Header header{};
    uint8 payload = 0;
    bool checksum_valid = false;
};

// Consumes the complete wire packet, including the two reply-clock bytes.
// Payload bytes are reported as they arrive so callers need not buffer a packet.
class PacketParser {
  public:
    ParseResult feed(uint8 byte);
    void reset();

  private:
    enum class State : uint8 {
        Sync0,
        Sync1,
        Command,
        Compression,
        LengthLo,
        LengthHi,
        Payload,
        ChecksumLo,
        ChecksumHi,
        DummyId,
        DummyStatus,
    };

    State state_ = State::Sync0;
    Header header_{};
    uint16 payload_read_ = 0;
    uint16 checksum_ = 0;
    uint16 received_checksum_ = 0;
};

}  // namespace Gbp::Protocol
