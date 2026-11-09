#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "pin_config.h"

#if KP_UNIT_TEST_LCD_0
#include "unit_test/lcd_0.h"
#endif

namespace {

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

void init_power_monitoring() {
    // These four signals are active-low open-drain outputs. The PCB has
    // external pull-ups; internal pull-ups also keep an unpopulated board sane.
    init_input(board::MP_BOOST_N, true);
    init_input(board::MP_ACOK_N, true);
    init_input(board::MP_CHARGE_N, true);
    init_input(board::CW_ALERT_N, true);

    i2c_init(board::FUEL_GAUGE_I2C, board::FUEL_GAUGE_I2C_BAUD);
    gpio_set_function(board::CW_SDA, GPIO_FUNC_I2C);
    gpio_set_function(board::CW_SCL, GPIO_FUNC_I2C);
}

void init_sd_bus() {
    // Keep the card deselected before assigning clock and data pins to SPI.
    init_output(board::SD_CS_N, true);
    spi_init(board::SD_SPI, board::SD_SPI_INITIAL_BAUD);
    gpio_set_function(board::SD_MISO, GPIO_FUNC_SPI);
    gpio_set_function(board::SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(board::SD_MOSI, GPIO_FUNC_SPI);
    init_input(board::SD_CARD_DETECT_N, true);
}

void init_keys() {
    init_input(board::KEY_RIGHT_N, true);
    init_input(board::KEY_DOWN_N, true);
    init_input(board::KEY_CONFIRM_N, true);
    init_input(board::KEY_LEFT_N, true);
    init_input(board::KEY_UP_N, true);
}

void init_game_boy_link() {
    // The link clock defaults to input so startup cannot contend with a Game
    // Boy. Data direction is fixed by the board-level translator.
    init_input(board::LINK_DATA_IN, false);
    init_input(board::LINK_CLOCK, false);
    init_output(board::LINK_DATA_OUT, true);
}

void init_board() {
    init_power_monitoring();
    init_sd_bus();
    init_keys();
    init_game_boy_link();
    init_input(board::RESERVED_GP15, false);
}

}  // namespace

int main() {
    // CMake enables only the RTT stdio backend; no UART pins are claimed.
    stdio_init_all();
    init_board();

#if KP_UNIT_TEST_LCD_0
    unit_test::lcd_0::init();
    printf("KP_UNIT_TEST_LCD_0 enabled\n");
#else
    printf("kuper-printer board initialization complete\n");
#endif

    while (true) {
#if KP_UNIT_TEST_LCD_0
        unit_test::lcd_0::update();
#endif
        tight_loop_contents();
    }
}
