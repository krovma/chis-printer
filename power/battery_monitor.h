// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <optional>

#include "types.h"
#include "pico/time.h"

namespace Power {

struct BatteryStatus {
    enum class Indication : uint8 { External, Percent, Low, Blank };

    Indication indication = Indication::External;
    uint8 percent = 0;
    bool charging = false;

    bool operator==(const BatteryStatus& other) const {
        return indication == other.indication && percent == other.percent && charging == other.charging;
    }
};

// Call on Core 0 after the board GPIO and I2C setup.
class BatteryMonitor {
  public:
    void initialize();
    BatteryStatus poll();

  private:
    absolute_time_t next_sample_{};
    std::optional<uint8> soc_{};
    std::optional<absolute_time_t> low_since_{};
    bool clear_alert_pending_ = false;
};

}  // namespace Power
