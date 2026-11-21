// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/printer_session.h"
#include "types.h"

namespace Gbp::Terminal {

ByteReply PrinterSession::feed(const uint8 byte, const bool buffer_full,
                               const uint32 now_us) {
    refresh_print_status(now_us);
    const Protocol::ParseResult result = parser_.feed(byte);
    switch (result.event) {
        case Protocol::Event::PacketComplete:
            packet_valid_ = result.checksum_valid;
            if (packet_valid_) {
                status_ &= static_cast<uint8>(~Protocol::STATUS_CHECKSUM_ERROR);
                apply_command(result.header);
            } else {
                status_ |= Protocol::STATUS_CHECKSUM_ERROR;
            }
            packet_reply_status_ = static_cast<uint8>(
                status_ | (buffer_full ? Protocol::STATUS_FULL : 0));
            return {Protocol::DEVICE_ID, false};
        case Protocol::Event::DummyId:
            return {packet_reply_status_, false};
        case Protocol::Event::DummyStatus:
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

void PrinterSession::abort_receive() {
    if (pending_print_started_at_us_.load(std::memory_order_acquire) == 0) {
        reset();
        return;
    }
    // Keep the PRINT timer and its completion notification while discarding
    // an interrupted packet or the next unfinished image.
    parser_.reset();
    packet_valid_ = false;
    packet_reply_status_ = 0;
    status_ &= Protocol::STATUS_FULL | Protocol::STATUS_BUSY;
}

void PrinterSession::refresh_print_status(const uint32 now_us) {
    if (!simulated_print_active_) {
        return;
    }
    const auto elapsed = static_cast<int32>(now_us - print_started_at_us_);
    if (elapsed < 0 || static_cast<uint32>(elapsed) < simulated_print_time_us_) {
        return;
    }
    simulated_print_active_ = false;
    status_ &= static_cast<uint8>(~Protocol::STATUS_BUSY);
}

bool PrinterSession::take_print_completion(const uint32 now_us, uint32& at_us) {
    uint32 started = pending_print_started_at_us_.load(std::memory_order_acquire);
    if (started == 0) {
        return false;
    }
    const auto elapsed = static_cast<int32>(now_us - started);
    if (elapsed < 0 || static_cast<uint32>(elapsed) < simulated_print_time_us_) {
        return false;
    }
    if (!pending_print_started_at_us_.compare_exchange_strong(started, 0, std::memory_order_acq_rel)) {
        return false;
    }
    at_us = now_us == 0 ? 1 : now_us;
    return true;
}

void PrinterSession::apply_command(const Protocol::Header& header) {
    switch (static_cast<Protocol::Command>(header.command)) {
        case Protocol::Command::Init:
            status_ = 0;
            if (simulated_print_active_) {
                pending_print_started_at_us_.store(0, std::memory_order_release);
            }
            simulated_print_active_ = false;
            break;
        case Protocol::Command::Break:
            status_ = Protocol::STATUS_FULL | Protocol::STATUS_BUSY;
            simulated_print_active_ = false;
            pending_print_started_at_us_.store(0, std::memory_order_release);
            break;
        default: break;
    }
}

void PrinterSession::finish_packet(const Protocol::Header& header, const uint32 now_us) {
    switch (static_cast<Protocol::Command>(header.command)) {
        case Protocol::Command::Data:
            if (header.length != 0) {
                status_ &= static_cast<uint8>(~Protocol::STATUS_FULL);
                status_ |= Protocol::STATUS_UNPROCESSED;
            }
            break;
        case Protocol::Command::Print:
            if (header.length >= 4) {
                // The PRINT reply still reports ready; busy begins after its status byte.
                print_started_at_us_ = now_us == 0 ? 1 : now_us;
                simulated_print_active_ = true;
                status_ &= static_cast<uint8>(~Protocol::STATUS_UNPROCESSED);
                status_ |= Protocol::STATUS_FULL | Protocol::STATUS_BUSY;
                pending_print_started_at_us_.store(print_started_at_us_, std::memory_order_release);
            }
            break;
        default:
            break;
    }
}

}  // namespace Gbp::Terminal
