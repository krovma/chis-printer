// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <atomic>

#include "types.h"
#include "gbp/terminal/receiver.h"
#include "gbp/terminal/printer_session.h"

namespace Gbp::Terminal {

// M1 Game Boy link slave. The clock IRQ only handles bits, status replies and
// a single-producer byte queue; image processing runs in poll() on the main loop.
class Link {
  public:
    void initialize(uint32 simulated_print_time_us);
    void stop();
    bool poll(Receiver& receiver);
    void set_buffer_full(bool full);
    bool take_print_completion(uint32& at_us);

  private:
    static constexpr uint32 QUEUE_SIZE = 4096;
    static void gpio_irq(unsigned int gpio, uint32 events);
    void on_rising_clock();
    void reset_wire();
    bool enqueue(uint8 byte);
    bool dequeue(uint8& byte);

    PrinterSession session_{};
    std::array<uint8, QUEUE_SIZE> queue_{};
    std::atomic<uint32> read_index_{0};
    std::atomic<uint32> write_index_{0};
    std::atomic<bool> overflow_{false};
    std::atomic<bool> buffer_full_{false};
    volatile uint32 last_byte_us_ = 0;
    volatile bool synced_ = false;
    uint16 preamble_ = 0;
    uint8 input_byte_ = 0;
    uint8 output_byte_ = 0;
    uint8 bit_count_ = 0;
};

}  // namespace Gbp::Terminal
