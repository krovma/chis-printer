// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "gbr/format.h"
#include "gbp/protocol/packet.h"
#include "gbp/protocol/rle.h"

namespace gbp::terminal {

inline constexpr std::size_t IMAGE_CAPACITY = 640 * 192;
inline constexpr std::size_t BYTES_PER_TILE_ROW = 20 * 16;
using ImageBuffer = std::array<std::uint8_t, IMAGE_CAPACITY>;

enum class ViewState : std::uint8_t { Waiting, Receiving, Preview };

class Receiver {
  public:
    explicit Receiver(ImageBuffer& image) : image_(image.data()) {}

    // Returns true when the screen content needs to be redrawn.
    bool feed(std::uint8_t byte);
    void abort_packet();
    void reset();
    void confirm_preview();
    void use_buffer(ImageBuffer& image);

    ViewState state() const { return state_; }
    std::uint32_t data_frames() const { return data_frames_; }
    std::size_t image_size() const { return image_size_; }
    const std::uint8_t* image_data() const { return image_; }
    std::uint8_t palette() const { return palette_; }
    const gbr::format::PrintControl& print_control() const { return print_control_; }
    bool buffer_full() const { return buffer_full_; }
    bool take_data_group_started();

  private:
    void begin_packet(const protocol::Header& header);
    void consume_payload(const protocol::Header& header, std::uint8_t byte);
    bool complete_packet(const protocol::ParseResult& packet);

    protocol::PacketParser parser_{};
    protocol::RleDecoder decoder_{};
    std::uint8_t* image_ = nullptr;
    ViewState state_ = ViewState::Waiting;
    std::size_t image_size_ = 0;
    std::size_t packet_start_size_ = 0;
    std::uint32_t data_frames_ = 0;
    std::uint8_t palette_ = 0xE4;
    gbr::format::PrintControl print_control_ = gbr::format::DEFAULT_PRINT_CONTROL;
    gbr::format::PrintControl pending_print_control_ = gbr::format::DEFAULT_PRINT_CONTROL;
    std::uint8_t print_payload_index_ = 0;
    bool packet_failed_ = false;
    bool packet_overflow_ = false;
    bool packet_open_ = false;
    bool buffer_full_ = false;
    bool group_started_ = false;
    bool data_group_started_ = false;
};

}  // namespace gbp::terminal
