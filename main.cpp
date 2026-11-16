#include <stdio.h>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "config/display_config.h"
#include "display/ui/screen.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"
#include "pin_config.h"
#include "storage/sd_card.h"

#if KP_UNIT_TEST_LCD_0
#include "unit_test/lcd_0.h"
#endif

namespace {

constexpr display::Gray4 STARTUP_GRAY     = 0x08;
constexpr display::Gray4 TEXT_GRAY        = 0x0F;
constexpr uint32_t       KEY_DEBOUNCE_MS  = 25;
constexpr uint32_t       SD_READY_DELAY_MS = 1000;
constexpr std::string_view SD_ERROR_TEXT = "SD CARD ERROR";

constexpr std::uint8_t KEY_RIGHT_MASK   = 1u << 0;
constexpr std::uint8_t KEY_DOWN_MASK    = 1u << 1;
constexpr std::uint8_t KEY_CONFIRM_MASK = 1u << 2;
constexpr std::uint8_t KEY_LEFT_MASK    = 1u << 3;
constexpr std::uint8_t KEY_UP_MASK      = 1u << 4;

display::ui::Locale current_locale = display::ui::DEFAULT_LOCALE;
display::ui::TextId current_text = display::ui::DEFAULT_TEXT;
std::uint8_t current_palette_key = display::WHITE_PALETTE_KEY;
std::uint8_t current_backlight_percent = display::BACKLIGHT_DUTY_PERCENT[display::DEFAULT_BACKLIGHT_KEY];

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

void init_sd_socket() {
    // FatFs takes ownership of the SPI pins when the card is mounted.
    init_output(board::SD_CS_N, true);
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
    init_sd_socket();
    init_keys();
    init_game_boy_link();
    init_input(board::RESERVED_GP15, false);
}

bool draw_current_message() {
    return display::ui::draw_message_screen({current_locale, current_text}, STARTUP_GRAY, TEXT_GRAY);
}

bool draw_sd_error() {
    return display::ui::draw_centered_text(SD_ERROR_TEXT, STARTUP_GRAY, TEXT_GRAY);
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
    if (delta < 0) {
        for (std::size_t index = display::BACKLIGHT_DUTY_PERCENT.size(); index > 0; --index) {
            const auto key = index - 1;
            const auto percent = display::BACKLIGHT_DUTY_PERCENT[key];
            if (percent < current_backlight_percent) {
                display::set_backlight_key(static_cast<std::uint8_t>(key));
                current_backlight_percent = percent;
                return true;
            }
        }
    } else if (delta > 0) {
        for (std::size_t key = 0; key < display::BACKLIGHT_DUTY_PERCENT.size(); ++key) {
            const auto percent = display::BACKLIGHT_DUTY_PERCENT[key];
            if (percent > current_backlight_percent) {
                display::set_backlight_key(static_cast<std::uint8_t>(key));
                current_backlight_percent = percent;
                return true;
            }
        }
    }
    return false;
}

void apply_initial_backlight(const std::uint8_t percent) {
    for (std::size_t key = 0; key < display::BACKLIGHT_DUTY_PERCENT.size(); ++key) {
        if (display::BACKLIGHT_DUTY_PERCENT[key] == percent) {
            display::set_backlight_key(static_cast<std::uint8_t>(key));
            return;
        }
    }
    display::set_backlight(static_cast<float>(percent));
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
    sleep_ms(SD_READY_DELAY_MS);
    const bool sd_ready = storage::mount_initialized_card();
    config::DisplayConfig display_config;
    if (sd_ready && !storage::load_display_config(display_config)) {
        printf("Using default display settings\n");
    }
    current_palette_key = display_config.palette_key;
    current_backlight_percent = display_config.backlight_percent;

    display::ui::initialize(current_locale, current_text);
    display::init();
    display::set_palette_key(current_palette_key);
    apply_initial_backlight(current_backlight_percent);
    bool screen_dirty = sd_ready ? draw_current_message() : draw_sd_error();
    if (!screen_dirty) {
        printf("Startup screen render failed\n");
    } else if (display::present()) {
        screen_dirty = false;
    }

    FiveWayKeyScanner key_scanner;
    if (sd_ready) {
        key_scanner.initialize();
    }
#endif

    while (true) {
#if KP_UNIT_TEST_LCD_0
        unit_test::lcd_0::update();
#else
        if (sd_ready) {
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
        }
        if (screen_dirty && !display::is_presenting() && display::present()) {
            screen_dirty = false;
        }
#endif
        tight_loop_contents();
    }
}
