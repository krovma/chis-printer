// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/receiver.h"

#include <algorithm>

#include "types.h"

namespace Gbp::Terminal {

bool Receiver::feed(const uint8 byte) {
    const Protocol::ParseResult result = parser_.feed(byte);
    if (state_ == ViewState::Preview) {
        return false;
    }

    switch (result.event) {
        case Protocol::Event::PacketStart:
            begin_packet(result.header);
            if (result.header.command == static_cast<uint8>(Protocol::Command::Data) && result.header.length != 0 &&
                !group_started_) {
                group_started_ = true;
                data_group_started_ = true;
                if (state_ == ViewState::Waiting) {
                    state_ = ViewState::Receiving;
                }
                return true;
            }
            if (state_ == ViewState::Waiting &&
                (result.header.command == static_cast<uint8>(Protocol::Command::Init) ||
                 (result.header.command == static_cast<uint8>(Protocol::Command::Data) && result.header.length != 0))) {
                state_ = ViewState::Receiving;
                return true;
            }
            break;
        case Protocol::Event::PayloadByte:
            consume_payload(result.header, result.payload);
            break;
        case Protocol::Event::PacketComplete:
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
    print_control_ = Gbr::Format::DEFAULT_PRINT_CONTROL;
    pending_print_control_ = Gbr::Format::DEFAULT_PRINT_CONTROL;
    print_payload_index_ = 0;
    packet_failed_ = false;
    packet_overflow_ = false;
    packet_open_ = false;
    buffer_full_ = false;
    group_started_ = false;
    data_group_started_ = false;
}

bool Receiver::take_data_group_started() {
    const bool started = data_group_started_;
    data_group_started_ = false;
    return started;
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

void Receiver::begin_packet(const Protocol::Header& header) {
    packet_start_size_ = image_size_;
    packet_open_ = true;
    packet_failed_ = false;
    packet_overflow_ = false;
    print_payload_index_ = 0;
    pending_print_control_ = print_control_;
    if (header.command == static_cast<uint8>(Protocol::Command::Data)) {
        packet_failed_ = header.compression > 1;
        decoder_.reset(header.compression == 1);
    }
}

void Receiver::consume_payload(const Protocol::Header& header, const uint8 byte) {
    if (header.command == static_cast<uint8>(Protocol::Command::Print)) {
        if (print_payload_index_ < pending_print_control_.size()) {
            pending_print_control_[print_payload_index_++] = byte;
        }
        return;
    }
    if (header.command != static_cast<uint8>(Protocol::Command::Data) || packet_failed_) {
        return;
    }

    uint8 decoded[129];
    const usize count = decoder_.feed(byte, decoded);
    if (count > IMAGE_CAPACITY - image_size_) {
        packet_failed_ = true;
        packet_overflow_ = true;
        return;
    }
    std::copy_n(decoded, count, image_ + image_size_);
    image_size_ += count;
}

bool Receiver::complete_packet(const Protocol::ParseResult& packet) {
    packet_open_ = false;
    if (!packet.checksum_valid) {
        image_size_ = packet_start_size_;
        return false;
    }

    switch (static_cast<Protocol::Command>(packet.header.command)) {
        case Protocol::Command::Init:
        case Protocol::Command::Break:
            image_size_ = 0;
            data_frames_ = 0;
            buffer_full_ = false;
            group_started_ = false;
            data_group_started_ = false;
            return true;
        case Protocol::Command::Data:
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
        case Protocol::Command::Print:
            if (packet.header.length < 4 || image_size_ < BYTES_PER_TILE_ROW || image_size_ % BYTES_PER_TILE_ROW != 0) {
                return false;
            }
            print_control_ = pending_print_control_;
            palette_ = Gbr::Format::preview_palette(print_control_[2]);
            state_ = ViewState::Preview;
            buffer_full_ = true;
            return true;
        default:
            return false;
    }
}

}  // namespace Gbp::Terminal
