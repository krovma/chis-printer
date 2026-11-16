#include "gbp/protocol/packet_writer.h"

namespace gbp::protocol {

std::size_t write_packet(const Command command, const bool compressed, const std::uint8_t* payload,
                         const std::uint16_t length, std::uint8_t* output, const std::size_t capacity) {
    const std::size_t packet_size = static_cast<std::size_t>(length) + 10;
    if (output == nullptr || capacity < packet_size || (length != 0 && payload == nullptr)) {
        return 0;
    }

    const std::uint8_t command_byte = static_cast<std::uint8_t>(command);
    const std::uint8_t compression_byte = compressed ? 1 : 0;
    const std::uint8_t length_lo = static_cast<std::uint8_t>(length);
    const std::uint8_t length_hi = static_cast<std::uint8_t>(length >> 8);
    std::uint16_t checksum = command_byte + compression_byte + length_lo + length_hi;
    output[0] = 0x88;
    output[1] = 0x33;
    output[2] = command_byte;
    output[3] = compression_byte;
    output[4] = length_lo;
    output[5] = length_hi;
    for (std::size_t i = 0; i < length; ++i) {
        output[6 + i] = payload[i];
        checksum += payload[i];
    }
    output[6 + length] = static_cast<std::uint8_t>(checksum);
    output[7 + length] = static_cast<std::uint8_t>(checksum >> 8);
    output[8 + length] = 0;
    output[9 + length] = 0;
    return packet_size;
}

}  // namespace gbp::protocol
