#include <stdio.h>
#include <cstdint>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "display/ui/screen.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"
#include "pin_config.h"

#if KP_UNIT_TEST_LCD_0
#include "unit_test/lcd_0.h"
#endif

namespace {

constexpr display::Gray4 STARTUP_GRAY = 0x08;
constexpr display::Gray4 TEXT_GRAY       = 0x0F;
constexpr uint32_t       KEY_DEBOUNCE_MS = 25;

constexpr std::uint8_t KEY_RIGHT_MASK   = 1u << 0;
constexpr std::uint8_t KEY_DOWN_MASK    = 1u << 1;
constexpr std::uint8_t KEY_CONFIRM_MASK = 1u << 2;
constexpr std::uint8_t KEY_LEFT_MASK    = 1u << 3;
constexpr std::uint8_t KEY_UP_MASK      = 1u << 4;

display::ui::Locale current_locale = display::ui::DEFAULT_LOCALE;
display::ui::TextId current_text    = display::ui::DEFAULT_TEXT;
std::uint8_t         current_palette_key   = display::WHITE_PALETTE_KEY;
std::uint8_t         current_backlight_key = display::DEFAULT_BACKLIGHT_KEY;

class FiveWayKeyScanner {
  public:
    void initialize() {
        const absolute_time_t now = get_absolute_time();
        raw_pressed_               = read_pressed_mask();
        stable_pressed_            = raw_pressed_;
        raw_changed_at_            = now;
    }

    std::uint8_t poll_press_mask() {
        const absolute_time_t now = get_absolute_time();
        const uint8_t raw_pressed = read_pressed_mask();

        if (raw_pressed != raw_pressed_) {
            raw_pressed_     = raw_pressed;
            raw_changed_at_  = now;
        }

        if (raw_pressed_ == stable_pressed_ ||
            absolute_time_diff_us(raw_changed_at_, now) < static_cast<int64_t>(KEY_DEBOUNCE_MS) * 1000) {
            return 0;
        }

        const uint8_t previous_stable = stable_pressed_;
        stable_pressed_               = raw_pressed_;
        return stable_pressed_ & static_cast<std::uint8_t>(~previous_stable);
    }

  private:
    static uint8_t read_pressed_mask() {
        uint8_t mask = 0;
        if (!gpio_get(board::KEY_RIGHT_N)) {
            mask |= 1u << 0;
        }
        if (!gpio_get(board::KEY_DOWN_N)) {
            mask |= 1u << 1;
        }
        if (!gpio_get(board::KEY_CONFIRM_N)) {
            mask |= 1u << 2;
        }
        if (!gpio_get(board::KEY_LEFT_N)) {
            mask |= 1u << 3;
        }
        if (!gpio_get(board::KEY_UP_N)) {
            mask |= 1u << 4;
        }
        return mask;
    }

    uint8_t         raw_pressed_      = 0;
    uint8_t         stable_pressed_   = 0;
    absolute_time_t raw_changed_at_{};
};

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

bool draw_current_message() {
    return display::ui::draw_message_screen({current_locale, current_text}, STARTUP_GRAY, TEXT_GRAY);
}

void advance_message() {
    const auto next = display::ui::next_message({current_locale, current_text});
    current_locale  = next.locale;
    current_text    = next.text;
    if (!draw_current_message()) {
        printf("display message render failed\n");
    }
}

bool adjust_palette(const int delta) {
    const int next_key = static_cast<int>(current_palette_key) + delta;
    if (next_key < 0 || next_key >= static_cast<int>(display::PALETTE_COUNT)) {
        return false;
    }

    if (!display::set_palette_key(static_cast<std::uint8_t>(next_key))) {
        return false;
    }
    current_palette_key = static_cast<std::uint8_t>(next_key);
    return true;
}

bool adjust_backlight(const int delta) {
    const int next_key = static_cast<int>(current_backlight_key) + delta;
    if (next_key < 0 || next_key >= static_cast<int>(display::BACKLIGHT_DUTY_PERCENT.size())) {
        return false;
    }

    if (!display::set_backlight_key(static_cast<std::uint8_t>(next_key))) {
        return false;
    }
    current_backlight_key = static_cast<std::uint8_t>(next_key);
    return true;
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
    display::ui::initialize(current_locale, current_text);
    display::init();
    display::set_palette_key(current_palette_key);
    display::set_backlight_key(current_backlight_key);
    bool screen_dirty = !draw_current_message();
    if (!screen_dirty) {
        screen_dirty = !display::present();
    }

    FiveWayKeyScanner key_scanner;
    key_scanner.initialize();
#endif

    while (true) {
#if KP_UNIT_TEST_LCD_0
        unit_test::lcd_0::update();
#else
        const std::uint8_t pressed_keys = key_scanner.poll_press_mask();
        if ((pressed_keys & KEY_UP_MASK) != 0 && adjust_palette(-1)) {
            screen_dirty = true;
        }
        if ((pressed_keys & KEY_DOWN_MASK) != 0 && adjust_palette(1)) {
            screen_dirty = true;
        }
        if ((pressed_keys & KEY_CONFIRM_MASK) != 0) {
            advance_message();
            screen_dirty = true;
        }
        if ((pressed_keys & KEY_LEFT_MASK) != 0) {
            adjust_backlight(-1);
        }
        if ((pressed_keys & KEY_RIGHT_MASK) != 0) {
            adjust_backlight(1);
        }
        if (screen_dirty && !display::is_presenting() && display::present()) {
            screen_dirty = false;
        }
#endif
        tight_loop_contents();
    }
}
