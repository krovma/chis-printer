// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"
#include "gbp/host/link_timing.h"
#include "gbp/host/print_job.h"

namespace Gbp::Host {

class GpioTransport final : public PacketTransport {
  public:
    explicit GpioTransport(uint8 rx_sample_delay_us = RX_SAMPLE_DELAY_DEFAULT_US);

    void begin();
    void end();
    bool send(Protocol::Command command, const uint8* payload,
              uint16 length, uint8& status) override;
    void pause_ms(uint32 milliseconds) override;

  private:
    uint8 exchange_byte(uint8 value);
    uint8 rx_sample_delay_us_ = RX_SAMPLE_DELAY_DEFAULT_US;
};

}  // namespace Gbp::Host
