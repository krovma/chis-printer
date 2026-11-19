// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/printer_session.h"

namespace gbp::terminal {

ByteReply PrinterSession::feed(const std::uint8_t byte, const bool buffer_full,
                               const std::uint32_t now_us) {
    refresh_print_status(now_us);
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
            packet_reply_status_ = static_cast<std::uint8_t>(
                status_ | (buffer_full ? protocol::STATUS_FULL : 0));
            return {protocol::DEVICE_ID, false};
        case protocol::Event::DummyId:
            return {packet_reply_status_, false};
        case protocol::Event::DummyStatus:
            if (packet_valid_) {
                finish_packet(result.header, now_us);
            }
            return {0, true};
        default:
            return {};
    }
}

void PrinterSession::reset() {
    parser_.reset();
    status_ = 0;
    packet_reply_status_ = 0;
    print_started_at_us_ = 0;
    pending_print_started_at_us_.store(0, std::memory_order_release);
    packet_valid_ = false;
    simulated_print_active_ = false;
}

void PrinterSession::refresh_print_status(const std::uint32_t now_us) {
    if (!simulated_print_active_) return;
    const auto elapsed = static_cast<std::int32_t>(now_us - print_started_at_us_);
    if (elapsed < 0 || static_cast<std::uint32_t>(elapsed) < simulated_print_time_us_) return;
    simulated_print_active_ = false;
    status_ &= static_cast<std::uint8_t>(~protocol::STATUS_BUSY);
}

bool PrinterSession::take_print_completion(const std::uint32_t now_us, std::uint32_t& at_us) {
    std::uint32_t started = pending_print_started_at_us_.load(std::memory_order_acquire);
    if (started == 0) return false;
    const auto elapsed = static_cast<std::int32_t>(now_us - started);
    if (elapsed < 0 || static_cast<std::uint32_t>(elapsed) < simulated_print_time_us_) return false;
    if (!pending_print_started_at_us_.compare_exchange_strong(started, 0, std::memory_order_acq_rel)) return false;
    at_us = now_us == 0 ? 1 : now_us;
    return true;
}

void PrinterSession::apply_command(const protocol::Header& header) {
    switch (static_cast<protocol::Command>(header.command)) {
        case protocol::Command::Init:
            status_ = 0;
            if (simulated_print_active_) {
                pending_print_started_at_us_.store(0, std::memory_order_release);
            }
            simulated_print_active_ = false;
            break;
        case protocol::Command::Break:
            status_ = protocol::STATUS_FULL | protocol::STATUS_BUSY;
            simulated_print_active_ = false;
            pending_print_started_at_us_.store(0, std::memory_order_release);
            break;
        default: break;
    }
}

void PrinterSession::finish_packet(const protocol::Header& header, const std::uint32_t now_us) {
    switch (static_cast<protocol::Command>(header.command)) {
        case protocol::Command::Data:
            if (header.length != 0) {
                status_ &= static_cast<std::uint8_t>(~protocol::STATUS_FULL);
                status_ |= protocol::STATUS_UNPROCESSED;
            }
            break;
        case protocol::Command::Print:
            if (header.length >= 4) {
                // The PRINT reply still reports ready; busy begins after its status byte.
                print_started_at_us_ = now_us == 0 ? 1 : now_us;
                simulated_print_active_ = true;
                status_ &= static_cast<std::uint8_t>(~protocol::STATUS_UNPROCESSED);
                status_ |= protocol::STATUS_FULL | protocol::STATUS_BUSY;
                pending_print_started_at_us_.store(print_started_at_us_, std::memory_order_release);
            }
            break;
        default:
            break;
    }
}

}  // namespace gbp::terminal
