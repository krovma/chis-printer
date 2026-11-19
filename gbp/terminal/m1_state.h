// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gbp/terminal/receiver.h"

namespace gbp::terminal {

// Core1 owns the receiver and the active buffer. Core0 owns a handed-off
// buffer until release_frame(), including while the SD writer is running.
class M1State {
  public:
    struct Frame {
        const std::uint8_t* data = nullptr;
        std::size_t size = 0;
        std::uint8_t palette = 0xE4;
        gbr::format::PrintControl print_control = gbr::format::DEFAULT_PRINT_CONTROL;
        std::uint32_t generation = 0;
        bool append_to_previous = false;
    };

    struct Snapshot {
        ViewState view = ViewState::Waiting;
        std::uint32_t frames = 0;
        std::uint32_t generation = 0;
    };

    explicit M1State(std::uint32_t auto_cut_time_limit_us = 1'000'000);

    Receiver& receiver() { return receiver_; }
    void core1_on_receiver_change(std::uint32_t now_us = 0);
    void core1_on_print_complete(std::uint32_t at_us);
    void core1_expire_completion_window(std::uint32_t now_us);
    void core1_process_confirmation();
    void request_confirmation(std::uint32_t generation);

    bool acquire_frame(Frame& frame);
    void release_frame();

    Snapshot snapshot() const;

  private:
    enum class Slot : std::uint8_t { Empty, Ready, Owned };

    std::array<ImageBuffer, 2> buffers_{};
    Receiver receiver_;
    std::atomic<Slot> slot_{Slot::Empty};
    // The view and preview generation are published in one atomic word.
    std::atomic<std::uint32_t> view_word_{0};
    std::atomic<std::uint32_t> frames_{0};
    std::atomic<std::uint32_t> confirmation_generation_{0};
    Frame handed_off_{};
    std::uint32_t generation_ = 0;
    std::uint8_t active_buffer_ = 0;
    std::uint32_t auto_cut_time_limit_us_ = 1'000'000;
    std::uint32_t print_completed_at_us_ = 0;
    bool completion_window_open_ = false;
    bool append_current_group_ = false;
};

}  // namespace gbp::terminal
