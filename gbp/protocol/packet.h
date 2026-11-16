#pragma once

#include <cstdint>

namespace gbp::protocol {

inline constexpr std::uint8_t DEVICE_ID = 0x81;
inline constexpr std::uint8_t STATUS_UNPROCESSED = 0x08;
inline constexpr std::uint8_t STATUS_FULL = 0x04;
inline constexpr std::uint8_t STATUS_BUSY = 0x02;
inline constexpr std::uint8_t STATUS_CHECKSUM_ERROR = 0x01;

enum class Command : std::uint8_t {
    Init = 0x01,
    Print = 0x02,
    Data = 0x04,
    Break = 0x08,
    Inquiry = 0x0F,
};

struct Header {
    std::uint8_t command = 0;
    std::uint8_t compression = 0;
    std::uint16_t length = 0;
};

enum class Event : std::uint8_t {
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
    std::uint8_t payload = 0;
    bool checksum_valid = false;
};

// Consumes the complete wire packet, including the two reply-clock bytes.
// Payload bytes are reported as they arrive so callers need not buffer a packet.
class PacketParser {
  public:
    ParseResult feed(std::uint8_t byte);
    void reset();

  private:
    enum class State : std::uint8_t {
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
    std::uint16_t payload_read_ = 0;
    std::uint16_t checksum_ = 0;
    std::uint16_t received_checksum_ = 0;
};

}  // namespace gbp::protocol
