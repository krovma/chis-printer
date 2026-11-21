// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"
#include "gbr/format.h"
#include "gbp/protocol/packet.h"
#include "gbp/protocol/rle.h"

namespace Gbp::Terminal {

inline constexpr usize IMAGE_CAPACITY = 640 * 192;
inline constexpr usize BYTES_PER_TILE_ROW = 20 * 16;
using ImageBuffer = std::array<uint8, IMAGE_CAPACITY>;

enum class ViewState : uint8 { Waiting, Receiving, Preview };

// Decodes packets into tiles; a failed data packet restores its starting size.
class Receiver {
  public:
    explicit Receiver(ImageBuffer& image) : image_(image.data()) {}

    // Returns true when the screen content needs to be redrawn.
    bool feed(uint8 byte);
    void abort_packet();
    void reset();
    void confirm_preview();
    void use_buffer(ImageBuffer& image);

    ViewState state() const { return state_; }
    uint32 data_frames() const { return data_frames_; }
    usize image_size() const { return image_size_; }
    const uint8* image_data() const { return image_; }
    uint8 palette() const { return palette_; }
    const Gbr::Format::PrintControl& print_control() const { return print_control_; }
    bool buffer_full() const { return buffer_full_; }
    bool take_data_group_started();

  private:
    void begin_packet(const Protocol::Header& header);
    void consume_payload(const Protocol::Header& header, uint8 byte);
    bool complete_packet(const Protocol::ParseResult& packet);

    Protocol::PacketParser parser_{};
    Protocol::RleDecoder decoder_{};
    uint8* image_ = nullptr;
    ViewState state_ = ViewState::Waiting;
    usize image_size_ = 0;
    usize packet_start_size_ = 0;
    uint32 data_frames_ = 0;
    uint8 palette_ = 0xE4;
    Gbr::Format::PrintControl print_control_ = Gbr::Format::DEFAULT_PRINT_CONTROL;
    Gbr::Format::PrintControl pending_print_control_ = Gbr::Format::DEFAULT_PRINT_CONTROL;
    uint8 print_payload_index_ = 0;
    bool packet_failed_ = false;
    bool packet_overflow_ = false;
    bool packet_open_ = false;
    bool buffer_full_ = false;
    bool group_started_ = false;
    bool data_group_started_ = false;
};

}  // namespace Gbp::Terminal
