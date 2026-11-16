#include "gbp/protocol/packet.h"

namespace gbp::protocol {

ParseResult PacketParser::feed(const std::uint8_t byte) {
    ParseResult result{};
    switch (state_) {
        case State::Sync0:
            if (byte == 0x88) {
                state_ = State::Sync1;
            }
            break;
        case State::Sync1:
            if (byte == 0x33) {
                state_ = State::Command;
                result.event = Event::Sync;
            } else {
                state_ = byte == 0x88 ? State::Sync1 : State::Sync0;
            }
            break;
        case State::Command:
            header_ = {};
            header_.command = byte;
            checksum_ = byte;
            state_ = State::Compression;
            break;
        case State::Compression:
            header_.compression = byte;
            checksum_ += byte;
            state_ = State::LengthLo;
            break;
        case State::LengthLo:
            header_.length = byte;
            checksum_ += byte;
            state_ = State::LengthHi;
            break;
        case State::LengthHi:
            header_.length |= static_cast<std::uint16_t>(byte) << 8;
            checksum_ += byte;
            payload_read_ = 0;
            state_ = header_.length == 0 ? State::ChecksumLo : State::Payload;
            result.event = Event::PacketStart;
            break;
        case State::Payload:
            checksum_ += byte;
            result.event = Event::PayloadByte;
            result.payload = byte;
            if (++payload_read_ == header_.length) {
                state_ = State::ChecksumLo;
            }
            break;
        case State::ChecksumLo:
            received_checksum_ = byte;
            state_ = State::ChecksumHi;
            break;
        case State::ChecksumHi:
            received_checksum_ |= static_cast<std::uint16_t>(byte) << 8;
            result.event = Event::PacketComplete;
            result.checksum_valid = received_checksum_ == checksum_;
            state_ = State::DummyId;
            break;
        case State::DummyId:
            result.event = Event::DummyId;
            state_ = State::DummyStatus;
            break;
        case State::DummyStatus:
            result.event = Event::DummyStatus;
            state_ = State::Sync0;
            break;
    }
    result.header = header_;
    return result;
}

void PacketParser::reset() {
    state_ = State::Sync0;
    header_ = {};
    payload_read_ = 0;
    checksum_ = 0;
    received_checksum_ = 0;
}

}  // namespace gbp::protocol
