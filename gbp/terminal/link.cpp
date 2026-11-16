// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/link.h"

#include <cstdio>

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#include "pin_config.h"

namespace gbp::terminal {
namespace {

Link* active_link = nullptr;
constexpr std::uint32_t PACKET_TIMEOUT_US = 400'000;

}  // namespace

void Link::initialize() {
    session_.reset();
    read_index_.store(0);
    write_index_.store(0);
    overflow_.store(false);
    buffer_full_.store(false);
    synced_ = false;
    preamble_ = 0;
    bit_count_ = 0;
    gpio_put(board::LINK_DATA_OUT, false);
    active_link = this;
    gpio_set_irq_enabled_with_callback(board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, true, &Link::gpio_irq);
    irq_set_priority(IO_IRQ_BANK0, 0x40);
}

void Link::gpio_irq(const unsigned int gpio, const std::uint32_t events) {
    if (active_link != nullptr && gpio == board::LINK_CLOCK && (events & GPIO_IRQ_EDGE_RISE) != 0) {
        active_link->on_rising_clock();
    }
}

void Link::on_rising_clock() {
    const std::uint8_t input_bit = gpio_get(board::LINK_DATA_IN) ? 1 : 0;
    if (!synced_) {
        preamble_ = static_cast<std::uint16_t>((preamble_ << 1) | input_bit);
        if (preamble_ == 0x8833) {
            synced_ = true;
            input_byte_ = 0;
            output_byte_ = 0;
            bit_count_ = 0;
            session_.feed(0x88, buffer_full_.load(std::memory_order_relaxed));
            session_.feed(0x33, buffer_full_.load(std::memory_order_relaxed));
            if (!enqueue(0x88) || !enqueue(0x33)) {
                overflow_.store(true, std::memory_order_relaxed);
            }
            last_byte_us_ = time_us_32();
        }
        gpio_put(board::LINK_DATA_OUT, false);
        return;
    }

    input_byte_ = static_cast<std::uint8_t>((input_byte_ << 1) | input_bit);
    if (++bit_count_ == 8) {
        const ByteReply reply = session_.feed(input_byte_, buffer_full_.load(std::memory_order_relaxed));
        if (!enqueue(input_byte_)) {
            overflow_.store(true, std::memory_order_relaxed);
        }
        last_byte_us_ = time_us_32();
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
    gpio_put(board::LINK_DATA_OUT, (output_byte_ & (0x80u >> bit_count_)) != 0);
}

bool Link::enqueue(const std::uint8_t byte) {
    const std::uint32_t write = write_index_.load(std::memory_order_relaxed);
    if (write - read_index_.load(std::memory_order_acquire) == QUEUE_SIZE) {
        return false;
    }
    queue_[write & (QUEUE_SIZE - 1)] = byte;
    write_index_.store(write + 1, std::memory_order_release);
    return true;
}

bool Link::dequeue(std::uint8_t& byte) {
    const std::uint32_t read = read_index_.load(std::memory_order_relaxed);
    if (read == write_index_.load(std::memory_order_acquire)) {
        return false;
    }
    byte = queue_[read & (QUEUE_SIZE - 1)];
    read_index_.store(read + 1, std::memory_order_release);
    return true;
}

void Link::reset_wire() {
    session_.reset();
    synced_ = false;
    preamble_ = 0;
    bit_count_ = 0;
    input_byte_ = 0;
    output_byte_ = 0;
    gpio_put(board::LINK_DATA_OUT, false);
}

bool Link::poll(Receiver& receiver) {
    bool changed = false;
    if (overflow_.exchange(false, std::memory_order_relaxed)) {
        printf("Game Boy link receive queue overflow\n");
        gpio_set_irq_enabled(board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, false);
        read_index_.store(write_index_.load(std::memory_order_acquire), std::memory_order_release);
        receiver.abort_packet();
        reset_wire();
        gpio_set_irq_enabled(board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, true);
    }

    std::uint8_t byte = 0;
    while (dequeue(byte)) {
        changed |= receiver.feed(byte);
    }
    set_buffer_full(receiver.buffer_full());

    if (synced_ && static_cast<std::uint32_t>(time_us_32() - last_byte_us_) > PACKET_TIMEOUT_US) {
        gpio_set_irq_enabled(board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, false);
        if (synced_ && static_cast<std::uint32_t>(time_us_32() - last_byte_us_) > PACKET_TIMEOUT_US) {
            receiver.abort_packet();
            reset_wire();
        }
        gpio_set_irq_enabled(board::LINK_CLOCK, GPIO_IRQ_EDGE_RISE, true);
    }
    return changed;
}

void Link::set_buffer_full(const bool full) {
    buffer_full_.store(full, std::memory_order_relaxed);
}

}  // namespace gbp::terminal
