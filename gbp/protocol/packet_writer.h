#pragma once

#include <cstddef>
#include <cstdint>

#include "gbp/protocol/packet.h"

namespace gbp::protocol {

// Encode a complete Game Boy Printer request, including two bytes that clock
// out the printer's device ID and status. Returns zero if output is too small.
std::size_t write_packet(Command command, bool compressed, const std::uint8_t* payload,
                         std::uint16_t length, std::uint8_t* output, std::size_t capacity);

}  // namespace gbp::protocol
