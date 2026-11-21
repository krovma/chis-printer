// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/link.h"

#include <cstdio>

#include "types.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#include "pin_config.h"

namespace Gbp::Terminal {
namespace {

Link* active_link = nullptr;
constexpr uint32 PACKET_TIMEOUT_US = 400'000;
// Allow normal gaps between packets while abandoning a disconnected image.
constexpr uint32 RECEIVE_IDLE_TIMEOUT_US = 5'000'000;

}  // namespace

void Link::initialize(const uint32 simulated_print_time_us) {
    session_.reset();
    session_.set_simulated_print_time(simulated_print_time_us);
    read_index_.store(0);
    write_index_.store(0);
    overflow_.store(false);
    buffer_full_.store(false);
    synced_ = false;
    preamble_ = 0;
    bit_count_ = 0;
    last_byte_us_ = time_us_32();
    gpio_put(Board::LINK_DATA_OUT, false);
    active_link = this;
    gpio_set_irq_enabled_with_callback(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, true, &Link::gpio_irq);
    irq_set_priority(IO_IRQ_BANK0, 0x40);
}

void Link::stop() {
    gpio_set_irq_enabled(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, false);
    active_link = nullptr;
    reset_wire();
    session_.reset();
    read_index_.store(0, std::memory_order_relaxed);
    write_index_.store(0, std::memory_order_relaxed);
    gpio_set_dir(Board::LINK_CLOCK, GPIO_IN);
    gpio_put(Board::LINK_DATA_OUT, true);
}

void Link::gpio_irq(const unsigned int gpio, const uint32 events) {
    if (active_link != nullptr && gpio == Board::LINK_CLOCK && (events & GPIO_IRQ_EDGE_RISE) != 0) {
        active_link->on_rising_clock();
    }
}

void Link::on_rising_clock() {
    const uint8 input_bit = gpio_get(Board::LINK_DATA_IN) ? 1 : 0;
    if (!synced_) {
        preamble_ = static_cast<uint16>((preamble_ << 1) | input_bit);
        if (preamble_ == 0x8833) {
            synced_ = true;
            input_byte_ = 0;
            output_byte_ = 0;
            bit_count_ = 0;
            const auto now_us = time_us_32();
            session_.feed(0x88, buffer_full_.load(std::memory_order_relaxed), now_us);
            session_.feed(0x33, buffer_full_.load(std::memory_order_relaxed), now_us);
            if (!enqueue(0x88) || !enqueue(0x33)) {
                overflow_.store(true, std::memory_order_relaxed);
            }
            last_byte_us_ = time_us_32();
        }
        gpio_put(Board::LINK_DATA_OUT, false);
        return;
    }

    input_byte_ = static_cast<uint8>((input_byte_ << 1) | input_bit);
    if (++bit_count_ == 8) {
        const auto now_us = time_us_32();
        const ByteReply reply = session_.feed(input_byte_, buffer_full_.load(std::memory_order_relaxed), now_us);
        if (!enqueue(input_byte_)) {
            overflow_.store(true, std::memory_order_relaxed);
        }
        last_byte_us_ = now_us;
        input_byte_ = 0;
        bit_count_ = 0;
        output_byte_ = reply.next_tx;
        if (reply.packet_end) {
            synced_ = false;
            preamble_ = 0;
            output_byte_ = 0;
        }
    }
    // The reference's rising-edge-only ISR prepares the following output bit
    // immediately after sampling this one, including at byte boundaries.
    gpio_put(Board::LINK_DATA_OUT, (output_byte_ & (0x80u >> bit_count_)) != 0);
}

bool Link::enqueue(const uint8 byte) {
    const uint32 write = write_index_.load(std::memory_order_relaxed);
    if (write - read_index_.load(std::memory_order_acquire) == QUEUE_SIZE) {
        return false;
    }
    queue_[write & (QUEUE_SIZE - 1)] = byte;
    write_index_.store(write + 1, std::memory_order_release);
    return true;
}

bool Link::dequeue(uint8& byte) {
    const uint32 read = read_index_.load(std::memory_order_relaxed);
    if (read == write_index_.load(std::memory_order_acquire)) {
        return false;
    }
    byte = queue_[read & (QUEUE_SIZE - 1)];
    read_index_.store(read + 1, std::memory_order_release);
    return true;
}

void Link::reset_wire() {
    session_.abort_receive();
    synced_ = false;
    preamble_ = 0;
    bit_count_ = 0;
    input_byte_ = 0;
    output_byte_ = 0;
    gpio_put(Board::LINK_DATA_OUT, false);
}

bool Link::poll(Receiver& receiver, bool& receive_aborted) {
    bool changed = false;
    receive_aborted = false;
    if (overflow_.exchange(false, std::memory_order_relaxed)) {
        printf("Game Boy link receive queue overflow\n");
        gpio_set_irq_enabled(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, false);
        read_index_.store(write_index_.load(std::memory_order_acquire), std::memory_order_release);
        receive_aborted = receiver.state() == ViewState::Receiving;
        if (receive_aborted) receiver.reset();
        else receiver.abort_packet();
        reset_wire();
        gpio_set_irq_enabled(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, true);
        set_buffer_full(receiver.buffer_full());
        return false;
    }

    uint8 byte = 0;
    while (dequeue(byte)) {
        if (receiver.feed(byte)) {
            changed = true;
            break;
        }
    }
    if (synced_ && read_index_.load(std::memory_order_relaxed) ==
                       write_index_.load(std::memory_order_acquire) &&
        static_cast<uint32>(time_us_32() - last_byte_us_) > PACKET_TIMEOUT_US) {
        gpio_set_irq_enabled(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, false);
        if (synced_ && read_index_.load(std::memory_order_relaxed) ==
                           write_index_.load(std::memory_order_acquire) &&
            static_cast<uint32>(time_us_32() - last_byte_us_) > PACKET_TIMEOUT_US) {
            receive_aborted = receive_aborted || receiver.state() == ViewState::Receiving;
            if (receive_aborted) receiver.reset();
            else receiver.abort_packet();
            reset_wire();
        }
        gpio_set_irq_enabled(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, true);
    }
    set_buffer_full(receiver.buffer_full());
    return changed;
}

bool Link::expire_receive(Receiver& receiver) {
    if (receiver.state() != ViewState::Receiving ||
        read_index_.load(std::memory_order_relaxed) != write_index_.load(std::memory_order_acquire) ||
        static_cast<uint32>(time_us_32() - last_byte_us_) <= RECEIVE_IDLE_TIMEOUT_US) {
        return false;
    }

    gpio_set_irq_enabled(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, false);
    const bool expired = receiver.state() == ViewState::Receiving &&
                         read_index_.load(std::memory_order_relaxed) ==
                             write_index_.load(std::memory_order_acquire) &&
                         static_cast<uint32>(time_us_32() - last_byte_us_) > RECEIVE_IDLE_TIMEOUT_US;
    if (expired) {
        receiver.reset();
        reset_wire();
        buffer_full_.store(false, std::memory_order_relaxed);
    }
    gpio_set_irq_enabled(Board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, true);
    return expired;
}

void Link::set_buffer_full(const bool full) {
    buffer_full_.store(full, std::memory_order_relaxed);
}

bool Link::take_print_completion(uint32& at_us) {
    return session_.take_print_completion(time_us_32(), at_us);
}

}  // namespace Gbp::Terminal
