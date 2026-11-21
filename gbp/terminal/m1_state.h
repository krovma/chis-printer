// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <atomic>

#include "types.h"
#include "gbp/terminal/receiver.h"

namespace Gbp::Terminal {

// Core1 owns the receiver and the active buffer. Core0 owns a handed-off
// buffer until release_frame(), including while the SD writer is running.
class M1State {
  public:
    struct Frame {
        const uint8* data = nullptr;
        usize size = 0;
        uint8 palette = 0xE4;
        Gbr::Format::PrintControl print_control = Gbr::Format::DEFAULT_PRINT_CONTROL;
        uint32 generation = 0;
        bool append_to_previous = false;
    };

    struct Snapshot {
        ViewState view = ViewState::Waiting;
        uint32 frames = 0;
        uint32 generation = 0;
        uint32 cut_sequence = 0;
        uint32 cut_remaining_us = 0;
        bool cut_pending = false;
        uint32 discarded_frames = 0;
    };

    explicit M1State(uint32 auto_cut_time_limit_us = 1'000'000);

    Receiver& receiver() { return receiver_; }
    void core1_on_receiver_change(uint32 now_us = 0);
    void core1_on_print_complete(uint32 at_us);
    bool core1_expire_completion_window(uint32 now_us);
    void core1_pause_after_cut();
    void core1_process_confirmation();
    void request_confirmation(uint32 generation);

    bool acquire_frame(Frame& frame);
    void release_frame();

    Snapshot snapshot(uint32 now_us = 0) const;
    uint32 cut_sequence() const { return cut_sequence_.load(std::memory_order_acquire); }

  private:
    enum class Slot : uint8 { Empty, Ready, Owned };

    std::array<ImageBuffer, 2> buffers_{};
    Receiver receiver_;
    std::atomic<Slot> slot_{Slot::Empty};
    // The view and preview generation are published in one atomic word.
    std::atomic<uint32> view_word_{0};
    std::atomic<uint32> frames_{0};
    std::atomic<uint32> confirmation_generation_{0};
    std::atomic<uint32> cut_sequence_{0};
    std::atomic<uint32> discarded_frames_{0};
    std::atomic<uint32> completed_at_published_us_{0};
    std::atomic_bool cut_pending_{false};
    Frame handed_off_{};
    uint32 generation_ = 0;
    uint8 active_buffer_ = 0;
    uint32 auto_cut_time_limit_us_ = 1'000'000;
    uint32 print_completed_at_us_ = 0;
    bool completion_window_open_ = false;
    bool append_current_group_ = false;
    void core1_publish_cut();
};

}  // namespace Gbp::Terminal
