// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "power/battery_monitor.h"

#include <atomic>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "pin_config.h"

namespace Power {
namespace {

constexpr uint8 REG_SOC       = 0x04;
constexpr uint8 REG_RRT_ALERT = 0x06;
constexpr uint8 REG_MODE      = 0x0A;
constexpr uint8 ALERT_FLAG    = 0x80;
constexpr uint8 MODE_SLEEP    = 0xC0;
constexpr uint8 MODE_NORMAL   = 0x00;

constexpr uint32 SAMPLE_INTERVAL_MS   = 10'000;
constexpr int64  BLINK_HALF_PERIOD_US  = 500'000;
constexpr uint32 TRANSFER_TIMEOUT_US  = 2'000;

std::atomic<bool> alert_pending{false};

void on_alert() {
    if (gpio_get_irq_event_mask(Board::CW_ALERT_N) & GPIO_IRQ_EDGE_FALL) {
        gpio_acknowledge_irq(Board::CW_ALERT_N, GPIO_IRQ_EDGE_FALL);
        alert_pending.store(true, std::memory_order_relaxed);
    }
}

bool transfer_succeeded(const int result, const int expected) {
    if (result == expected) {
        return true;
    }
    // Reset the controller so the next poll can recover after an interrupted transfer.
    i2c_init(Board::FUEL_GAUGE_I2C, Board::FUEL_GAUGE_I2C_BAUD);
    return false;
}

bool read_register(const uint8 reg, uint8& value) {
    const auto deadline = make_timeout_time_us(TRANSFER_TIMEOUT_US);
    return transfer_succeeded(i2c_write_blocking_until(Board::FUEL_GAUGE_I2C, Board::FUEL_GAUGE_I2C_ADDRESS,
                                                      &reg, 1, true, deadline), 1) &&
           transfer_succeeded(i2c_read_blocking_until(Board::FUEL_GAUGE_I2C, Board::FUEL_GAUGE_I2C_ADDRESS,
                                                     &value, 1, false, deadline), 1);
}

bool write_register(const uint8 reg, const uint8 value) {
    const uint8 data[]{reg, value};
    return transfer_succeeded(i2c_write_timeout_us(Board::FUEL_GAUGE_I2C, Board::FUEL_GAUGE_I2C_ADDRESS,
                                                   data, sizeof(data), false, TRANSFER_TIMEOUT_US), sizeof(data));
}

std::optional<uint8> read_soc() {
    uint8 mode = 0;
    if (!read_register(REG_MODE, mode)) {
        return {};
    }
    if ((mode & MODE_SLEEP) == MODE_SLEEP) {
        // Allow the gauge to update its estimate before the next sample.
        write_register(REG_MODE, MODE_NORMAL);
        return {};
    }

    // The high SOC byte gives the percentage in 1% steps.
    uint8 percent = 0;
    if (!read_register(REG_SOC, percent) || percent > 100) {
        return {};
    }
    return percent;
}

bool clear_alert() {
    uint8 rrt_alert = 0;
    return read_register(REG_RRT_ALERT, rrt_alert) &&
           write_register(REG_RRT_ALERT, static_cast<uint8>(rrt_alert & ~ALERT_FLAG));
}

}  // namespace

void BatteryMonitor::initialize() {
    gpio_add_raw_irq_handler(Board::CW_ALERT_N, on_alert);
    gpio_set_irq_enabled(Board::CW_ALERT_N, GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
    if (!gpio_get(Board::CW_ALERT_N)) {
        alert_pending.store(true, std::memory_order_relaxed);
    }
}

BatteryStatus BatteryMonitor::poll() {
    using Indication = BatteryStatus::Indication;
    const auto now = get_absolute_time();
    const bool charging = !gpio_get(Board::MP_CHARGE_N);
    const bool new_alert = alert_pending.exchange(false, std::memory_order_relaxed);
    if (charging) {
        low_since_.reset();
    } else if (new_alert && !low_since_) {
        low_since_ = now;
    }
    clear_alert_pending_ |= new_alert;

    const bool sample_due = absolute_time_diff_us(next_sample_, now) >= 0;
    if (sample_due) {
        next_sample_ = delayed_by_ms(now, SAMPLE_INTERVAL_MS);
        soc_ = read_soc();
    }
    if ((new_alert || sample_due) && clear_alert_pending_ && soc_) {
        clear_alert_pending_ = !clear_alert();
        if (clear_alert_pending_) {
            soc_.reset();
        }
    }

    if (!soc_) {
        return {Indication::External, 0, charging};
    }
    if (low_since_) {
        const auto half_period = absolute_time_diff_us(*low_since_, now) / BLINK_HALF_PERIOD_US;
        return {half_period % 2 == 0 ? Indication::Low : Indication::Blank, 0, false};
    }
    return {Indication::Percent, *soc_, charging};
}

}  // namespace Power
