// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

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
            {
            const bool print_complete = completion_reply_pending_;
            completion_reply_pending_ = false;
            if (packet_valid_) {
                finish_packet(result.header);
            }
            return {0, true, print_complete};
            }
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
    print_pending_ = false;
    completion_reply_pending_ = false;
}

void PrinterSession::apply_command(const protocol::Header& header) {
    switch (static_cast<protocol::Command>(header.command)) {
        case protocol::Command::Init:
            status_ = 0;
            print_pending_ = false;
            data_countdown_ = 6;
            unprocessed_countdown_ = 0;
            busy_countdown_ = 0;
            break;
        case protocol::Command::Print:
            print_pending_ = true;
            busy_countdown_ = 1;
            break;
        case protocol::Command::Data:
            unprocessed_countdown_ = 3;
            break;
        case protocol::Command::Break:
            status_ = protocol::STATUS_FULL | protocol::STATUS_BUSY;
            print_pending_ = false;
            break;
        case protocol::Command::Inquiry:
            if (unprocessed_countdown_ > 0) {
                if (--unprocessed_countdown_ == 0 && busy_countdown_ > 0) {
                    status_ |= protocol::STATUS_FULL | protocol::STATUS_BUSY;
                }
            } else if (busy_countdown_ > 0 && --busy_countdown_ == 0) {
                status_ &= static_cast<std::uint8_t>(~protocol::STATUS_BUSY);
            }
            if (print_pending_ && busy_countdown_ == 0 && unprocessed_countdown_ == 0 &&
                (status_ & protocol::STATUS_BUSY) == 0) {
                completion_reply_pending_ = true;
                print_pending_ = false;
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
