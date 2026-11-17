// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/receiver.h"

#include <algorithm>

namespace gbp::terminal {

bool Receiver::feed(const std::uint8_t byte) {
    const protocol::ParseResult result = parser_.feed(byte);
    if (state_ == ViewState::Preview) {
        return false;
    }

    switch (result.event) {
        case protocol::Event::PacketStart:
            begin_packet(result.header);
            if (state_ == ViewState::Waiting &&
                (result.header.command == static_cast<std::uint8_t>(protocol::Command::Init) ||
                 (result.header.command == static_cast<std::uint8_t>(protocol::Command::Data) &&
                  result.header.length != 0))) {
                state_ = ViewState::Receiving;
                return true;
            }
            break;
        case protocol::Event::PayloadByte:
            consume_payload(result.header, result.payload);
            break;
        case protocol::Event::PacketComplete:
            return complete_packet(result);
        default:
            break;
    }
    return false;
}

void Receiver::abort_packet() {
    parser_.reset();
    if (packet_open_ && state_ != ViewState::Preview) {
        image_size_ = packet_start_size_;
    }
    packet_open_ = false;
    packet_failed_ = false;
    packet_overflow_ = false;
}

void Receiver::reset() {
    parser_.reset();
    decoder_.reset(false);
    state_ = ViewState::Waiting;
    image_size_ = 0;
    packet_start_size_ = 0;
    data_frames_ = 0;
    palette_ = 0xE4;
    print_control_ = format::DEFAULT_PRINT_CONTROL;
    pending_print_control_ = format::DEFAULT_PRINT_CONTROL;
    print_payload_index_ = 0;
    packet_failed_ = false;
    packet_overflow_ = false;
    packet_open_ = false;
    buffer_full_ = false;
}

void Receiver::confirm_preview() {
    if (state_ == ViewState::Preview) {
        reset();
    }
}

void Receiver::use_buffer(ImageBuffer& image) {
    image_ = image.data();
    reset();
}

void Receiver::begin_packet(const protocol::Header& header) {
    packet_start_size_ = image_size_;
    packet_open_ = true;
    packet_failed_ = false;
    packet_overflow_ = false;
    print_payload_index_ = 0;
    pending_print_control_ = print_control_;
    if (header.command == static_cast<std::uint8_t>(protocol::Command::Data)) {
        packet_failed_ = header.compression > 1;
        decoder_.reset(header.compression == 1);
    }
}

void Receiver::consume_payload(const protocol::Header& header, const std::uint8_t byte) {
    if (header.command == static_cast<std::uint8_t>(protocol::Command::Print)) {
        if (print_payload_index_ < pending_print_control_.size()) {
            pending_print_control_[print_payload_index_++] = byte;
        }
        return;
    }
    if (header.command != static_cast<std::uint8_t>(protocol::Command::Data) || packet_failed_) {
        return;
    }

    std::uint8_t decoded[129];
    const std::size_t count = decoder_.feed(byte, decoded);
    if (count > IMAGE_CAPACITY - image_size_) {
        packet_failed_ = true;
        packet_overflow_ = true;
        return;
    }
    std::copy_n(decoded, count, image_ + image_size_);
    image_size_ += count;
}

bool Receiver::complete_packet(const protocol::ParseResult& packet) {
    packet_open_ = false;
    if (!packet.checksum_valid) {
        image_size_ = packet_start_size_;
        return false;
    }

    switch (static_cast<protocol::Command>(packet.header.command)) {
        case protocol::Command::Init:
        case protocol::Command::Break:
            image_size_ = 0;
            data_frames_ = 0;
            buffer_full_ = false;
            return true;
        case protocol::Command::Data:
            if (packet.header.length == 0) {
                return false;
            }
            if (packet_failed_ || !decoder_.complete()) {
                image_size_ = packet_start_size_;
                if (packet_overflow_) {
                    buffer_full_ = true;
                }
                return false;
            }
            ++data_frames_;
            return true;
        case protocol::Command::Print:
            if (packet.header.length < 4 || image_size_ < BYTES_PER_TILE_ROW ||
                image_size_ % BYTES_PER_TILE_ROW != 0) {
                return false;
            }
            print_control_ = pending_print_control_;
            palette_ = format::preview_palette(print_control_[2]);
            state_ = ViewState::Preview;
            buffer_full_ = true;
            return true;
        default:
            return false;
    }
}

}  // namespace gbp::terminal
