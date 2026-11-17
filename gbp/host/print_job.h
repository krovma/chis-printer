// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gbp/format/gbr.h"
#include "gbp/protocol/packet.h"

namespace gbp::host {

enum class PrintResult : std::uint8_t { None, Success, InvalidImage, NoPrinter, PrinterError, Timeout };

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
    virtual bool send(protocol::Command command, const std::uint8_t* payload,
                      std::uint16_t length, std::uint8_t& status) = 0;
    virtual void pause_ms(std::uint32_t milliseconds) = 0;
};

class PrintJob {
  public:
    PrintResult run(PacketTransport& transport, const std::uint8_t* data, std::size_t size,
                    const format::PrintControl& print_control, std::atomic<std::size_t>& bytes_sent);

  private:
    PrintResult send(PacketTransport& transport, protocol::Command command, const std::uint8_t* payload,
                     std::uint16_t length, std::uint8_t& status);
    PrintResult wait_ready(PacketTransport& transport, const char* phase,
                           std::uint8_t mask, std::uint32_t attempts);
};

}  // namespace gbp::host
