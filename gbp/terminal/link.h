// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "gbp/terminal/receiver.h"
#include "gbp/terminal/printer_session.h"

namespace gbp::terminal {

// M1 Game Boy link slave. The clock IRQ only handles bits, status replies and
// a single-producer byte queue; image processing runs in poll() on the main loop.
class Link {
  public:
    void initialize(std::uint32_t simulated_print_time_us);
    void stop();
    bool poll(Receiver& receiver);
    void set_buffer_full(bool full);
    bool take_print_completion(std::uint32_t& at_us);

  private:
    static constexpr std::uint32_t QUEUE_SIZE = 4096;
    static void gpio_irq(unsigned int gpio, std::uint32_t events);
    void on_rising_clock();
    void reset_wire();
    bool enqueue(std::uint8_t byte);
    bool dequeue(std::uint8_t& byte);

    PrinterSession session_{};
    std::array<std::uint8_t, QUEUE_SIZE> queue_{};
    std::atomic<std::uint32_t> read_index_{0};
    std::atomic<std::uint32_t> write_index_{0};
    std::atomic<bool> overflow_{false};
    std::atomic<bool> buffer_full_{false};
    volatile std::uint32_t last_byte_us_ = 0;
    volatile bool synced_ = false;
    std::uint16_t preamble_ = 0;
    std::uint8_t input_byte_ = 0;
    std::uint8_t output_byte_ = 0;
    std::uint8_t bit_count_ = 0;
};

}  // namespace gbp::terminal
