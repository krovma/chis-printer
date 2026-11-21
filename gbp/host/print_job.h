// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <atomic>

#include "types.h"
#include "gbr/format.h"
#include "gbp/protocol/packet.h"

namespace Gbp::Host {

enum class PrintResult : uint8 { None, Success, InvalidImage, NoPrinter, PrinterError, Timeout };

constexpr const char* print_result_name(const PrintResult result) {
    switch (result) {
        case PrintResult::None: return "none";
        case PrintResult::Success: return "success";
        case PrintResult::InvalidImage: return "invalid image";
        case PrintResult::NoPrinter: return "no printer response";
        case PrintResult::PrinterError: return "printer status error";
        case PrintResult::Timeout: return "status timeout";
    }
    return "unknown";
}

class PacketTransport {
  public:
    virtual ~PacketTransport() = default;
    virtual bool send(Protocol::Command command, const uint8* payload,
                      uint16 length, uint8& status) = 0;
    virtual void pause_ms(uint32 milliseconds) = 0;
};

class PrintJob {
  public:
    PrintResult run(PacketTransport& transport, const uint8* data, usize size,
                    const Gbr::Format::PrintControl& print_control, std::atomic<usize>& bytes_sent);

  private:
    PrintResult send(PacketTransport& transport, Protocol::Command command, const uint8* payload,
                     uint16 length, uint8& status);
    PrintResult wait_ready(PacketTransport& transport, const char* phase,
                           uint8 mask, uint32 attempts);
};

}  // namespace Gbp::Host
