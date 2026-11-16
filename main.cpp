#include <stdio.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "config/display_config.h"
#include "display/ui/screen.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"
#include "gbp/terminal/link.h"
#include "gbp/terminal/screen.h"
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

std::uint8_t current_palette_key = display::WHITE_PALETTE_KEY;
std::uint8_t current_backlight_percent = display::BACKLIGHT_DUTY_PERCENT[display::DEFAULT_BACKLIGHT_KEY];
gbp::terminal::Receiver receiver;
gbp::terminal::Link link;
std::atomic<bool> sd_ready{false};
std::atomic<bool> preview_confirm_requested{false};
std::atomic<std::uint32_t> visible_state{static_cast<std::uint32_t>(gbp::terminal::ViewState::Waiting)};
std::atomic<std::uint32_t> visible_frames{0};
alignas(8) std::array<std::uint32_t, 1024> link_core_stack{};

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

bool draw_sd_error() {
    return display::ui::draw_centered_text(SD_ERROR_TEXT, STARTUP_GRAY, TEXT_GRAY);
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

#if !KP_UNIT_TEST_LCD_0
void run_link_core() {
    link.initialize();
    while (true) {
        if (preview_confirm_requested.load(std::memory_order_acquire)) {
            receiver.confirm_preview();
            link.set_buffer_full(false);
            visible_frames.store(0, std::memory_order_release);
            visible_state.store(static_cast<std::uint32_t>(gbp::terminal::ViewState::Waiting),
                                std::memory_order_release);
            preview_confirm_requested.store(false, std::memory_order_release);
        }
        if (link.poll(receiver)) {
            visible_frames.store(receiver.data_frames(), std::memory_order_release);
            visible_state.store(static_cast<std::uint32_t>(receiver.state()), std::memory_order_release);
        }
        tight_loop_contents();
    }
}

bool draw_m1_state(const gbp::terminal::ViewState state, const std::uint32_t frames) {
    switch (state) {
        case gbp::terminal::ViewState::Waiting:
            return gbp::terminal::draw_waiting();
        case gbp::terminal::ViewState::Receiving:
            return gbp::terminal::draw_receiving(frames);
        case gbp::terminal::ViewState::Preview:
            gbp::terminal::draw_preview(receiver);
            return true;
    }
    return false;
}

void run_ui_core() {
    const bool card_ready = sd_ready.load(std::memory_order_acquire);
    display::ui::initialize();
    display::init();
    display::set_palette_key(current_palette_key);
    apply_initial_backlight(current_backlight_percent);

    bool screen_dirty = !(card_ready ? gbp::terminal::draw_waiting() : draw_sd_error());
    if (!screen_dirty && display::present()) {
        while (display::is_presenting()) {
            tight_loop_contents();
        }
    } else {
        screen_dirty = true;
    }

    FiveWayKeyScanner key_scanner;
    if (card_ready) {
        key_scanner.initialize();
        multicore_launch_core1_with_stack(run_link_core, link_core_stack.data(), sizeof(link_core_stack));
    }

    gbp::terminal::ViewState drawn_state = gbp::terminal::ViewState::Waiting;
    std::uint32_t drawn_frames = 0;
    bool confirmation_pending = false;
    while (true) {
        const auto state = static_cast<gbp::terminal::ViewState>(visible_state.load(std::memory_order_acquire));
        const std::uint32_t frames = visible_frames.load(std::memory_order_acquire);
        if (confirmation_pending && state != gbp::terminal::ViewState::Preview) {
            confirmation_pending = false;
        }
        if (card_ready && (state != drawn_state ||
                           (state == gbp::terminal::ViewState::Receiving && frames != drawn_frames))) {
            screen_dirty = true;
        }

        if (card_ready) {
            const std::uint8_t pressed_keys = key_scanner.poll_press_mask();
            if ((pressed_keys & KEY_UP_MASK) != 0 && adjust_palette(-1)) {
                screen_dirty = true;
            }
            if ((pressed_keys & KEY_DOWN_MASK) != 0 && adjust_palette(1)) {
                screen_dirty = true;
            }
            if ((pressed_keys & KEY_CONFIRM_MASK) != 0 && state == gbp::terminal::ViewState::Preview) {
                confirmation_pending = true;
                preview_confirm_requested.store(true, std::memory_order_release);
            }
            if ((pressed_keys & KEY_LEFT_MASK) != 0) {
                adjust_backlight(-1);
            }
            if ((pressed_keys & KEY_RIGHT_MASK) != 0) {
                adjust_backlight(1);
            }
        }

        if (screen_dirty && !display::is_presenting()) {
            const auto next_state = static_cast<gbp::terminal::ViewState>(visible_state.load(std::memory_order_acquire));
            const std::uint32_t next_frames = visible_frames.load(std::memory_order_acquire);
            if (next_state != gbp::terminal::ViewState::Preview || !confirmation_pending) {
                const bool rendered = card_ready ? draw_m1_state(next_state, next_frames) : draw_sd_error();
                if (rendered && display::present()) {
                    drawn_state = next_state;
                    drawn_frames = next_frames;
                    screen_dirty = false;
                }
            }
        }
        tight_loop_contents();
    }
}
#endif

}  // namespace

int main() {
    // CMake enables only the RTT stdio backend; no UART pins are claimed.
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
    const bool card_ready = storage::mount_initialized_card();
    config::DisplayConfig display_config;
    if (card_ready && !storage::load_display_config(display_config)) {
        printf("Using default display settings\n");
    }
    current_palette_key = display_config.palette_key;
    current_backlight_percent = display_config.backlight_percent;
    sd_ready.store(card_ready, std::memory_order_release);

    run_ui_core();
#endif
}
