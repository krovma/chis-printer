// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gbp/format/gbr.h"
#include "gbp/host/link_timing.h"
#include "gbp/host/print_job.h"
#include "gbp/terminal/link.h"
#include "gbp/terminal/m1_state.h"

namespace gbp::terminal {

// Core0 issues one command at a time; Core1 publishes mode/result after it
// has released the pins. M1State and print data stay owned by Core0 until ack.
class Worker {
  public:
    enum class Mode : std::uint8_t { Starting, Idle, Receiving, Printing };

    void run();
    void start_receive(M1State& state);
    void stop_receive();
    void start_print(const std::uint8_t* data, std::size_t size,
                     const format::PrintControl& control, std::uint8_t rx_sample_delay_us);

    Mode mode() const { return mode_.load(std::memory_order_acquire); }
    host::PrintResult print_result() const { return print_result_.load(std::memory_order_acquire); }
    std::size_t print_progress() const { return print_progress_.load(std::memory_order_acquire); }

  private:
    enum class Command : std::uint8_t { None, Receive, Stop, Print };

    Link link_{};
    std::atomic<Command> command_{Command::None};
    std::atomic<Mode> mode_{Mode::Starting};
    std::atomic<host::PrintResult> print_result_{host::PrintResult::None};
    std::atomic<std::size_t> print_progress_{0};
    M1State* m1_ = nullptr;
    const std::uint8_t* print_data_ = nullptr;
    std::size_t print_size_ = 0;
    format::PrintControl print_control_ = format::DEFAULT_PRINT_CONTROL;
    std::uint8_t print_rx_sample_delay_us_ = host::RX_SAMPLE_DELAY_DEFAULT_US;
};

}  // namespace gbp::terminal
