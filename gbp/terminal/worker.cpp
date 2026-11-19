// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/worker.h"

#include "gbp/host/gpio_transport.h"
#include "pico/stdlib.h"

namespace gbp::terminal {

void Worker::start_receive(M1State& state) {
    m1_ = &state;
    mode_.store(Mode::Starting, std::memory_order_release);
    command_.store(Command::Receive, std::memory_order_release);
}

void Worker::stop_receive() {
    mode_.store(Mode::Starting, std::memory_order_release);
    command_.store(Command::Stop, std::memory_order_release);
}

void Worker::start_print(const std::uint8_t* data, const std::size_t size,
                         const gbr::format::PrintControl& control, const std::uint8_t rx_sample_delay_us) {
    print_data_ = data;
    print_size_ = size;
    print_control_ = control;
    print_rx_sample_delay_us_ = rx_sample_delay_us;
    print_progress_.store(0, std::memory_order_release);
    print_result_.store(host::PrintResult::None, std::memory_order_release);
    command_.store(Command::Print, std::memory_order_release);
}

void Worker::run() {
    mode_.store(Mode::Idle, std::memory_order_release);
    while (true) {
        const Command command = command_.exchange(Command::None, std::memory_order_acq_rel);
        if (command == Command::Receive) {
            link_.initialize();
            mode_.store(Mode::Receiving, std::memory_order_release);
        } else if (command == Command::Stop) {
            link_.stop();
            m1_ = nullptr;
            mode_.store(Mode::Idle, std::memory_order_release);
        } else if (command == Command::Print) {
            mode_.store(Mode::Printing, std::memory_order_release);
            host::GpioTransport transport{print_rx_sample_delay_us_};
            host::PrintJob job;
            transport.begin();
            const host::PrintResult result = job.run(transport, print_data_, print_size_,
                                                      print_control_, print_progress_);
            transport.end();
            print_result_.store(result, std::memory_order_release);
            mode_.store(Mode::Idle, std::memory_order_release);
        }

        if (mode_.load(std::memory_order_relaxed) == Mode::Receiving) {
            std::uint32_t completed_at_us = 0;
            if (link_.take_print_completion(completed_at_us)) {
                m1_->core1_on_print_complete(completed_at_us);
            }
            m1_->core1_process_confirmation();
            if (link_.poll(m1_->receiver())) {
                m1_->core1_on_receiver_change(time_us_32());
                link_.set_buffer_full(m1_->receiver().buffer_full());
            }
            m1_->core1_expire_completion_window(time_us_32());
        }
        tight_loop_contents();
    }
}

}  // namespace gbp::terminal
