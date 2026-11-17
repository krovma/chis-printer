// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kuper Printer firmware.
// Copyright (C) 2026 Ken(github.com/krovma)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include <stdio.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/regs/addressmap.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "config/display_config.h"
#include "display/ui/screen.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"
#include "gbp/terminal/link.h"
#include "gbp/terminal/m1_state.h"
#include "gbp/terminal/screen.h"
#include "pin_config.h"
#include "storage/gbp_store.h"
#include "storage/sd_card.h"

#if KP_UNIT_TEST_LCD_0
#include "unit_test/lcd_0.h"
#endif

#if !KP_UNIT_TEST_LCD_0
extern "C" {
extern char __end__;
extern char __StackLimit;
extern char __StackBottom;
extern char __StackTop;
void* _sbrk(int increment);
}
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
#if !KP_UNIT_TEST_LCD_0
gbp::terminal::M1State m1_state;
gbp::terminal::Link link;
storage::GbpStore gbp_store;
std::atomic<bool> sd_ready{false};
alignas(8) std::array<std::uint32_t, 1024> link_core_stack{};
#endif

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
void report_sram_usage() {
    const auto ram_start = static_cast<std::uintptr_t>(SRAM_BASE);
    const auto ram_end = reinterpret_cast<std::uintptr_t>(&__StackLimit);
    const auto static_end = reinterpret_cast<std::uintptr_t>(&__end__);
    const auto heap_end = reinterpret_cast<std::uintptr_t>(_sbrk(0));
    const auto stack_bottom = reinterpret_cast<std::uintptr_t>(&__StackBottom);
    const auto stack_top = reinterpret_cast<std::uintptr_t>(&__StackTop);
    std::uintptr_t stack_pointer = 0;
    __asm volatile("mov %0, sp" : "=r"(stack_pointer));

    const auto main_used = heap_end - ram_start;
    const auto main_free = heap_end <= ram_end ? ram_end - heap_end : 0;
    const auto core0_stack_used = stack_top - stack_pointer;
    printf("SRAM main=%lu/%lu free=%lu static=%lu heap=%lu core0_stack=%lu/%lu B\n",
           static_cast<unsigned long>(main_used), static_cast<unsigned long>(ram_end - ram_start),
           static_cast<unsigned long>(main_free), static_cast<unsigned long>(static_end - ram_start),
           static_cast<unsigned long>(heap_end - static_end), static_cast<unsigned long>(core0_stack_used),
           static_cast<unsigned long>(stack_top - stack_bottom));
}

void run_link_core() {
    link.initialize();
    while (true) {
        m1_state.core1_process_confirmation();
        if (link.poll(m1_state.receiver())) {
            m1_state.core1_on_receiver_change();
            link.set_buffer_full(m1_state.receiver().buffer_full());
        }
        tight_loop_contents();
    }
}

bool draw_m1_state(const gbp::terminal::ViewState state, const std::uint32_t frames,
                   const gbp::terminal::M1State::Frame* preview) {
    switch (state) {
        case gbp::terminal::ViewState::Waiting:
            return gbp::terminal::draw_waiting();
        case gbp::terminal::ViewState::Receiving:
            return gbp::terminal::draw_receiving(frames);
        case gbp::terminal::ViewState::Preview:
            if (preview != nullptr) {
                gbp::terminal::draw_preview(preview->data, preview->size, preview->palette);
                return true;
            }
            return false;
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
    std::uint32_t drawn_generation = 0;
    bool confirmation_pending = false;
    std::uint32_t confirmation_generation = 0;
    gbp::terminal::M1State::Frame active_frame{};
    bool frame_owned = false;
    bool frame_displayed_or_skipped = false;
    bool save_active = false;
    absolute_time_t next_sram_report = make_timeout_time_ms(100);
    while (true) {
        if (time_reached(next_sram_report)) {
            report_sram_usage();
            next_sram_report = make_timeout_time_ms(100);
        }
        if (card_ready && !frame_owned && m1_state.acquire_frame(active_frame)) {
            frame_owned = true;
            frame_displayed_or_skipped = false;
            save_active = gbp_store.begin(active_frame.data, active_frame.size, active_frame.print_control);
            screen_dirty = true;
        }

        const auto snapshot = m1_state.snapshot();
        const auto state = snapshot.view;
        const std::uint32_t frames = snapshot.frames;
        const std::uint32_t generation = snapshot.generation;
        if (confirmation_pending &&
            (state != gbp::terminal::ViewState::Preview || generation != confirmation_generation)) {
            confirmation_pending = false;
        }
        if (frame_owned && (state != gbp::terminal::ViewState::Preview ||
                            generation != active_frame.generation || confirmation_pending)) {
            frame_displayed_or_skipped = true;
        }
        if (card_ready && (state != drawn_state ||
                           (state == gbp::terminal::ViewState::Receiving && frames != drawn_frames) ||
                           (state == gbp::terminal::ViewState::Preview && generation != drawn_generation))) {
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
                confirmation_generation = generation;
                m1_state.request_confirmation(generation);
            }
            if ((pressed_keys & KEY_LEFT_MASK) != 0) {
                adjust_backlight(-1);
            }
            if ((pressed_keys & KEY_RIGHT_MASK) != 0) {
                adjust_backlight(1);
            }
        }

        if (screen_dirty && !display::is_presenting()) {
            const auto next_snapshot = m1_state.snapshot();
            const auto next_state = next_snapshot.view;
            const std::uint32_t next_frames = next_snapshot.frames;
            const std::uint32_t next_generation = next_snapshot.generation;
            if (next_state != gbp::terminal::ViewState::Preview || !confirmation_pending) {
                const auto* preview = frame_owned && active_frame.generation == next_generation ? &active_frame : nullptr;
                const bool reuse_preview = next_state == gbp::terminal::ViewState::Preview && preview == nullptr &&
                                           drawn_state == gbp::terminal::ViewState::Preview &&
                                           drawn_generation == next_generation;
                const bool rendered = card_ready ?
                    (reuse_preview || draw_m1_state(next_state, next_frames, preview)) : draw_sd_error();
                if (rendered && display::present()) {
                    drawn_state = next_state;
                    drawn_frames = next_frames;
                    drawn_generation = next_generation;
                    if (next_state == gbp::terminal::ViewState::Preview && preview != nullptr) {
                        frame_displayed_or_skipped = true;
                    }
                    screen_dirty = false;
                }
            }
        }

        if (save_active) {
            const auto result = gbp_store.step();
            if (result == storage::GbpStore::StepResult::Complete ||
                result == storage::GbpStore::StepResult::Error) {
                save_active = false;
            }
        }
        if (frame_owned && !save_active && frame_displayed_or_skipped) {
            m1_state.release_frame();
            frame_owned = false;
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
    const bool mounted = storage::mount_initialized_card();
    config::DisplayConfig display_config;
    if (mounted && !storage::load_display_config(display_config)) {
        printf("Using default display settings\n");
    }
    const bool card_ready = mounted && gbp_store.initialize();
    current_palette_key = display_config.palette_key;
    current_backlight_percent = display_config.backlight_percent;
    sd_ready.store(card_ready, std::memory_order_release);

    run_ui_core();
#endif
}
