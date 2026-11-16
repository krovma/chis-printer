#include "gbp/terminal/printer_session.h"

namespace gbp::terminal {

ByteReply PrinterSession::feed(const std::uint8_t byte, const bool buffer_full) {
    const protocol::ParseResult result = parser_.feed(byte);
    switch (result.event) {
        case protocol::Event::PacketComplete:
            packet_valid_ = result.checksum_valid;
            if (packet_valid_) {
                status_ &= static_cast<std::uint8_t>(~protocol::STATUS_CHECKSUM_ERROR);
                apply_command(result.header);
            } else {
                status_ |= protocol::STATUS_CHECKSUM_ERROR;
            }
            return {protocol::DEVICE_ID, false};
        case protocol::Event::DummyId:
            return {static_cast<std::uint8_t>(status_ | (buffer_full ? protocol::STATUS_FULL : 0)), false};
        case protocol::Event::DummyStatus:
            if (packet_valid_) {
                finish_packet(result.header);
            }
            return {0, true};
        default:
            return {};
    }
}

void PrinterSession::reset() {
    parser_.reset();
    status_ = 0;
    busy_countdown_ = 0;
    unprocessed_countdown_ = 0;
    data_countdown_ = 0;
    packet_valid_ = false;
}

void PrinterSession::apply_command(const protocol::Header& header) {
    switch (static_cast<protocol::Command>(header.command)) {
        case protocol::Command::Init:
            status_ = 0;
            data_countdown_ = 6;
            unprocessed_countdown_ = 0;
            busy_countdown_ = 0;
            break;
        case protocol::Command::Print:
            busy_countdown_ = 1;
            break;
        case protocol::Command::Data:
            unprocessed_countdown_ = 3;
            break;
        case protocol::Command::Break:
            status_ = protocol::STATUS_FULL | protocol::STATUS_BUSY;
            break;
        case protocol::Command::Inquiry:
            if (unprocessed_countdown_ > 0) {
                if (--unprocessed_countdown_ == 0 && busy_countdown_ > 0) {
                    status_ |= protocol::STATUS_FULL | protocol::STATUS_BUSY;
                }
            } else if (busy_countdown_ > 0 && --busy_countdown_ == 0) {
                status_ &= static_cast<std::uint8_t>(~protocol::STATUS_BUSY);
            }
            break;
    }
}

void PrinterSession::finish_packet(const protocol::Header& header) {
    switch (static_cast<protocol::Command>(header.command)) {
        case protocol::Command::Data:
            if (data_countdown_ > 0) {
                --data_countdown_;
            }
            status_ &= static_cast<std::uint8_t>(~(protocol::STATUS_FULL | protocol::STATUS_UNPROCESSED));
            if (header.length == 0) {
                status_ |= protocol::STATUS_FULL;
            }
            break;
        case protocol::Command::Inquiry:
            status_ &= static_cast<std::uint8_t>(~protocol::STATUS_UNPROCESSED);
            if (unprocessed_countdown_ == 0 && busy_countdown_ == 0) {
                status_ &= static_cast<std::uint8_t>(~protocol::STATUS_FULL);
            }
            break;
        default:
            break;
    }
}

}  // namespace gbp::terminal
