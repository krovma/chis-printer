// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>

namespace gbp::host {

inline constexpr std::uint32_t CLOCK_HALF_PERIOD_US = 60;
inline constexpr std::uint8_t RX_SAMPLE_DELAY_MIN_US = 2;
inline constexpr std::uint8_t RX_SAMPLE_DELAY_MAX_US = 10;
inline constexpr std::uint8_t RX_SAMPLE_DELAY_DEFAULT_US = 5;

static_assert(RX_SAMPLE_DELAY_MAX_US < CLOCK_HALF_PERIOD_US);

constexpr bool valid_rx_sample_delay_us(const std::int64_t value) {
    return value >= RX_SAMPLE_DELAY_MIN_US && value <= RX_SAMPLE_DELAY_MAX_US;
}

}  // namespace gbp::host
