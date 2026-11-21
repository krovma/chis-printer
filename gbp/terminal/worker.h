// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <atomic>

#include "types.h"
#include "gbr/format.h"
#include "gbp/host/link_timing.h"
#include "gbp/host/print_job.h"
#include "gbp/terminal/link.h"
#include "gbp/terminal/m1_state.h"

namespace Gbp::Terminal {

// Core0 issues one command at a time; Core1 publishes mode/result after it
// has released the pins. M1State and print data stay owned by Core0 until ack.
class Worker {
  public:
    enum class Mode : uint8 { Starting, Idle, Receiving, Printing };

    void run();
    void start_receive(M1State& state, bool pause_on_auto_cut = false,
                       uint32 simulated_print_time_us = 1'000'000);
    void stop_receive();
    void start_print(const uint8* data, usize size,
                     const Gbr::Format::PrintControl& control, uint8 rx_sample_delay_us);

    Mode mode() const { return mode_.load(std::memory_order_acquire); }
    Host::PrintResult print_result() const { return print_result_.load(std::memory_order_acquire); }
    usize print_progress() const { return print_progress_.load(std::memory_order_acquire); }

  private:
    enum class Command : uint8 { None, Receive, Stop, Print };

    Link link_{};
    std::atomic<Command> command_{Command::None};
    std::atomic<Mode> mode_{Mode::Starting};
    std::atomic<Host::PrintResult> print_result_{Host::PrintResult::None};
    std::atomic<usize> print_progress_{0};
    M1State* m1_ = nullptr;
    bool pause_on_auto_cut_ = false;
    uint32 simulated_print_time_us_ = 1'000'000;
    const uint8* print_data_ = nullptr;
    usize print_size_ = 0;
    Gbr::Format::PrintControl print_control_ = Gbr::Format::DEFAULT_PRINT_CONTROL;
    uint8 print_rx_sample_delay_us_ = Host::RX_SAMPLE_DELAY_DEFAULT_US;
};

}  // namespace Gbp::Terminal
