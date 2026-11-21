// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/protocol/packet_writer.h"
#include "types.h"

namespace Gbp::Protocol {

usize write_packet(const Command command, const bool compressed, const uint8* payload,
                         const uint16 length, uint8* output, const usize capacity) {
    const usize packet_size = static_cast<usize>(length) + 10;
    if (output == nullptr || capacity < packet_size || (length != 0 && payload == nullptr)) {
        return 0;
    }

    const uint8 command_byte = static_cast<uint8>(command);
    const uint8 compression_byte = compressed ? 1 : 0;
    const uint8 length_lo = static_cast<uint8>(length);
    const uint8 length_hi = static_cast<uint8>(length >> 8);
    uint16 checksum = command_byte + compression_byte + length_lo + length_hi;
    output[0] = 0x88;
    output[1] = 0x33;
    output[2] = command_byte;
    output[3] = compression_byte;
    output[4] = length_lo;
    output[5] = length_hi;
    for (usize i = 0; i < length; ++i) {
        output[6 + i] = payload[i];
        checksum += payload[i];
    }
    output[6 + length] = static_cast<uint8>(checksum);
    output[7 + length] = static_cast<uint8>(checksum >> 8);
    output[8 + length] = 0;
    output[9 + length] = 0;
    return packet_size;
}

}  // namespace Gbp::Protocol
