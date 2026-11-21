// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/worker.h"

#include <cstdio>

#include "types.h"

#include "gbp/host/gpio_transport.h"
#include "pico/stdlib.h"

namespace Gbp::Terminal {

void Worker::start_receive(M1State& state, const bool pause_on_auto_cut,
                           const uint32 simulated_print_time_us) {
    m1_ = &state;
    pause_on_auto_cut_ = pause_on_auto_cut;
    simulated_print_time_us_ = simulated_print_time_us;
    mode_.store(Mode::Starting, std::memory_order_release);
    command_.store(Command::Receive, std::memory_order_release);
}

void Worker::stop_receive() {
    mode_.store(Mode::Starting, std::memory_order_release);
    command_.store(Command::Stop, std::memory_order_release);
}

void Worker::start_print(const uint8* data, const usize size,
                         const Gbr::Format::PrintControl& control, const uint8 rx_sample_delay_us) {
    print_data_ = data;
    print_size_ = size;
    print_control_ = control;
    print_rx_sample_delay_us_ = rx_sample_delay_us;
    print_progress_.store(0, std::memory_order_release);
    print_result_.store(Host::PrintResult::None, std::memory_order_release);
    command_.store(Command::Print, std::memory_order_release);
}

void Worker::run() {
    mode_.store(Mode::Idle, std::memory_order_release);
    while (true) {
        const Command command = command_.exchange(Command::None, std::memory_order_acq_rel);
        if (command == Command::Receive) {
            link_.initialize(simulated_print_time_us_);
            mode_.store(Mode::Receiving, std::memory_order_release);
        } else if (command == Command::Stop) {
            link_.stop();
            m1_ = nullptr;
            mode_.store(Mode::Idle, std::memory_order_release);
        } else if (command == Command::Print) {
            mode_.store(Mode::Printing, std::memory_order_release);
            Host::GpioTransport transport{print_rx_sample_delay_us_};
            Host::PrintJob job;
            transport.begin();
            const Host::PrintResult result = job.run(transport, print_data_, print_size_,
                                                      print_control_, print_progress_);
            transport.end();
            print_result_.store(result, std::memory_order_release);
            mode_.store(Mode::Idle, std::memory_order_release);
        }

        if (mode_.load(std::memory_order_relaxed) == Mode::Receiving) {
            const uint32 cut_before_poll = m1_->cut_sequence();
            uint32 completed_at_us = 0;
            if (link_.take_print_completion(completed_at_us)) {
                m1_->core1_on_print_complete(completed_at_us);
            }
            m1_->core1_process_confirmation();
            bool cut_expired = m1_->core1_expire_completion_window(time_us_32());
            bool receive_aborted = false;
            if (!cut_expired) {
                const uint32 stale_frames = m1_->receiver().data_frames();
                const bool changed = link_.poll(m1_->receiver(), receive_aborted);
                if (receive_aborted) {
                    m1_->core1_on_receive_timeout(time_us_32());
                    printf("M1 receive interrupted: discarded %lu data packets\n",
                           static_cast<unsigned long>(stale_frames));
                } else if (changed) {
                    m1_->core1_on_receiver_change(time_us_32());
                }
            }
            const uint32 stale_frames = m1_->receiver().data_frames();
            if (link_.expire_receive(m1_->receiver())) {
                m1_->core1_on_receive_timeout(time_us_32());
                printf("M1 receive timeout: discarded %lu data packets\n",
                       static_cast<unsigned long>(stale_frames));
            }
            cut_expired = m1_->core1_expire_completion_window(time_us_32()) || cut_expired ||
                          m1_->cut_sequence() != cut_before_poll;
            if (cut_expired && pause_on_auto_cut_) {
                link_.stop();
                m1_->core1_pause_after_cut();
                m1_ = nullptr;
                mode_.store(Mode::Idle, std::memory_order_release);
            }
        }
        tight_loop_contents();
    }
}

}  // namespace Gbp::Terminal
