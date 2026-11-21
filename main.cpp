// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <cstdio>

#include "types.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "app/application.h"
#include "config/cvar.h"
#include "pin_config.h"
#include "storage/sd_card.h"

#if KP_UNIT_TEST_LCD_0
#include "unit_test/lcd_0.h"
#endif

namespace {

constexpr uint32 SD_READY_DELAY_MS = 1000;

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
    init_input(Board::MP_BOOST_N, true);
    init_input(Board::MP_ACOK_N, true);
    init_input(Board::MP_CHARGE_N, true);
    init_input(Board::CW_ALERT_N, true);
    i2c_init(Board::FUEL_GAUGE_I2C, Board::FUEL_GAUGE_I2C_BAUD);
    gpio_set_function(Board::CW_SDA, GPIO_FUNC_I2C);
    gpio_set_function(Board::CW_SCL, GPIO_FUNC_I2C);

    init_output(Board::SD_CS_N, true);
    init_input(Board::SD_CARD_DETECT_N, true);
    init_input(Board::KEY_RIGHT_N, true);
    init_input(Board::KEY_DOWN_N, true);
    init_input(Board::KEY_CONFIRM_N, true);
    init_input(Board::KEY_LEFT_N, true);
    init_input(Board::KEY_UP_N, true);

    // Only the master mode in M2 drives the link clock.
    init_input(Board::LINK_DATA_IN, false);
    init_input(Board::LINK_CLOCK, false);
    init_output(Board::LINK_DATA_OUT, true);
    init_input(Board::RESERVED_GP15, false);
}

}  // namespace

int main() {
    // RTT is the only enabled stdio backend.
    stdio_init_all();
    init_board();

#if KP_UNIT_TEST_LCD_0
    UnitTest::Lcd0::init();
    printf("KP_UNIT_TEST_LCD_0 enabled\n");
    while (true) {
        UnitTest::Lcd0::update();
        tight_loop_contents();
    }
#else
    sleep_ms(SD_READY_DELAY_MS);
    const bool mounted = Storage::mount_initialized_card();
    if (mounted && !Config::Controller::instance().reload()) {
        printf("Using default firmware settings\n");
    }
    App::run(mounted);
#endif
}
