// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <cstdio>
#include <cstdint>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "app/application.h"
#include "config/firmware_config.h"
#include "pin_config.h"
#include "storage/sd_card.h"

#if KP_UNIT_TEST_LCD_0
#include "unit_test/lcd_0.h"
#endif

namespace {

constexpr std::uint32_t SD_READY_DELAY_MS = 1000;

void init_input(const uint pin, const bool pull_up) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    if (pull_up) {
        gpio_pull_up(pin);
    } else {
        gpio_disable_pulls(pin);
    }
}

void init_output(const uint pin, const bool initial_level) {
    gpio_init(pin);
    gpio_put(pin, initial_level);
    gpio_set_dir(pin, GPIO_OUT);
}

void init_board() {
    // External pull-ups are present on these active-low open-drain signals.
    init_input(board::MP_BOOST_N, true);
    init_input(board::MP_ACOK_N, true);
    init_input(board::MP_CHARGE_N, true);
    init_input(board::CW_ALERT_N, true);
    i2c_init(board::FUEL_GAUGE_I2C, board::FUEL_GAUGE_I2C_BAUD);
    gpio_set_function(board::CW_SDA, GPIO_FUNC_I2C);
    gpio_set_function(board::CW_SCL, GPIO_FUNC_I2C);

    init_output(board::SD_CS_N, true);
    init_input(board::SD_CARD_DETECT_N, true);
    init_input(board::KEY_RIGHT_N, true);
    init_input(board::KEY_DOWN_N, true);
    init_input(board::KEY_CONFIRM_N, true);
    init_input(board::KEY_LEFT_N, true);
    init_input(board::KEY_UP_N, true);

    // Only the master mode in M2 drives the link clock.
    init_input(board::LINK_DATA_IN, false);
    init_input(board::LINK_CLOCK, false);
    init_output(board::LINK_DATA_OUT, true);
    init_input(board::RESERVED_GP15, false);
}

}  // namespace

int main() {
    // RTT is the only enabled stdio backend.
    stdio_init_all();
    init_board();

#if KP_UNIT_TEST_LCD_0
    unit_test::lcd_0::init();
    printf("KP_UNIT_TEST_LCD_0 enabled\n");
    while (true) {
        unit_test::lcd_0::update();
        tight_loop_contents();
    }
#else
    sleep_ms(SD_READY_DELAY_MS);
    const bool mounted = storage::mount_initialized_card();
    config::FirmwareConfig firmware_config;
    if (mounted && !storage::load_firmware_config(firmware_config)) {
        printf("Using default firmware settings\n");
    }
    app::run(mounted, firmware_config);
#endif
}
