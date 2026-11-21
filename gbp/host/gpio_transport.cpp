// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/host/gpio_transport.h"

#include <array>
#include <cstdio>

#include "types.h"
#include "gbp/protocol/packet_writer.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pin_config.h"

namespace Gbp::Host {

GpioTransport::GpioTransport(const uint8 rx_sample_delay_us)
    : rx_sample_delay_us_(valid_rx_sample_delay_us(rx_sample_delay_us) ?
                          rx_sample_delay_us : RX_SAMPLE_DELAY_DEFAULT_US) {}

void GpioTransport::begin() {
    gpio_set_dir(Board::LINK_DATA_IN, GPIO_IN);
    gpio_disable_pulls(Board::LINK_DATA_IN);
    gpio_put(Board::LINK_DATA_OUT, true);
    gpio_set_dir(Board::LINK_DATA_OUT, GPIO_OUT);
    gpio_put(Board::LINK_CLOCK, true);
    gpio_set_dir(Board::LINK_CLOCK, GPIO_OUT);
    printf("GBP link master SC=GP%u TX=GP%u RX=GP%u idle_rx=%u rx_delay=%u us\n",
           static_cast<unsigned>(Board::LINK_CLOCK), static_cast<unsigned>(Board::LINK_DATA_OUT),
           static_cast<unsigned>(Board::LINK_DATA_IN), static_cast<unsigned>(gpio_get(Board::LINK_DATA_IN)),
           static_cast<unsigned>(rx_sample_delay_us_));
}

void GpioTransport::end() {
    gpio_put(Board::LINK_CLOCK, true);
    gpio_set_dir(Board::LINK_CLOCK, GPIO_IN);
    gpio_put(Board::LINK_DATA_OUT, true);
}

uint8 GpioTransport::exchange_byte(const uint8 value) {
    uint8 received = 0;
    for (int bit = 7; bit >= 0; --bit) {
        gpio_put(Board::LINK_CLOCK, false);
        gpio_put(Board::LINK_DATA_OUT, (value & (1u << bit)) != 0);
        sleep_us(CLOCK_HALF_PERIOD_US);
        gpio_put(Board::LINK_CLOCK, true);
        // The printer response settles shortly after the rising edge on the
        // measured TXS0104E link; sampling immediately reads the edge transient.
        sleep_us(rx_sample_delay_us_);
        received = static_cast<uint8>((received << 1) | (gpio_get(Board::LINK_DATA_IN) ? 1u : 0u));
        sleep_us(CLOCK_HALF_PERIOD_US - rx_sample_delay_us_);
    }
    sleep_us(200);
    return received;
}

bool GpioTransport::send(const Protocol::Command command, const uint8* payload,
                         const uint16 length, uint8& status) {
    status = 0;
    std::array<uint8, 650> packet{};
    const usize packet_size = Protocol::write_packet(command, false, payload, length,
                                                            packet.data(), packet.size());
    if (packet_size == 0) {
        printf("ERROR: GBP packet encode cmd=0x%02X len=%u capacity=%lu\n",
               static_cast<unsigned>(command), static_cast<unsigned>(length),
               static_cast<unsigned long>(packet.size()));
        return false;
    }
    uint8 id = 0;
    std::array<uint8, 6> response_head{};
    std::array<uint8, 4> response_tail{};
    uint32 low_bits = 0;
    for (usize i = 0; i < packet_size; ++i) {
        const uint8 received = exchange_byte(packet[i]);
        if (i < response_head.size()) {
            response_head[i] = received;
        }
        if (i >= packet_size - response_tail.size()) {
            response_tail[i - (packet_size - response_tail.size())] = received;
        }
        for (unsigned bit = 0; bit < 8; ++bit) {
            low_bits += (received & (1u << bit)) == 0;
        }
        if (i == packet_size - 2) {
            id = received;
        }
        if (i == packet_size - 1) {
            status = received;
        }
    }
    if (id != Protocol::DEVICE_ID) {
        printf("ERROR: GBP device ID cmd=0x%02X len=%u expected=0x%02X got=0x%02X "
               "status=0x%02X rx_tail=%02X %02X %02X %02X\n",
               static_cast<unsigned>(command), static_cast<unsigned>(length),
               static_cast<unsigned>(Protocol::DEVICE_ID), static_cast<unsigned>(id),
               static_cast<unsigned>(status), static_cast<unsigned>(response_tail[0]),
               static_cast<unsigned>(response_tail[1]), static_cast<unsigned>(response_tail[2]),
               static_cast<unsigned>(response_tail[3]));
        printf("GBP RX low_bits=%lu/%lu first=%02X %02X %02X %02X %02X %02X\n",
               static_cast<unsigned long>(low_bits), static_cast<unsigned long>(packet_size * 8),
               static_cast<unsigned>(response_head[0]), static_cast<unsigned>(response_head[1]),
               static_cast<unsigned>(response_head[2]), static_cast<unsigned>(response_head[3]),
               static_cast<unsigned>(response_head[4]), static_cast<unsigned>(response_head[5]));
        return false;
    }
    if ((status & Protocol::STATUS_ERROR_MASK) != 0) {
        printf("GBP raw error response cmd=0x%02X rx_tail=%02X %02X %02X %02X\n",
               static_cast<unsigned>(command), static_cast<unsigned>(response_tail[0]),
               static_cast<unsigned>(response_tail[1]), static_cast<unsigned>(response_tail[2]),
               static_cast<unsigned>(response_tail[3]));
    }
    return true;
}

void GpioTransport::pause_ms(const uint32 milliseconds) { sleep_ms(milliseconds); }

}  // namespace Gbp::Host
