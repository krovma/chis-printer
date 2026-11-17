// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>

#include "gbp/host/link_timing.h"
#include "gbp/host/print_job.h"

namespace gbp::host {

class GpioTransport final : public PacketTransport {
  public:
    explicit GpioTransport(std::uint8_t rx_sample_delay_us = RX_SAMPLE_DELAY_DEFAULT_US);

    void begin();
    void end();
    bool send(protocol::Command command, const std::uint8_t* payload,
              std::uint16_t length, std::uint8_t& status) override;
    void pause_ms(std::uint32_t milliseconds) override;

  private:
    std::uint8_t exchange_byte(std::uint8_t value);
    std::uint8_t rx_sample_delay_us_ = RX_SAMPLE_DELAY_DEFAULT_US;
};

}  // namespace gbp::host
