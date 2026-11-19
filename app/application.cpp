// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/application.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "app/screen.h"
#include "app/state_machine.h"
#include "bt/bluetooth_printer.h"
#include "display/ui/screen.h"
#include "display/zjy154s0800.h"
#include "gbr/format.h"
#include "gbp/terminal/m1_state.h"
#include "gbp/terminal/screen.h"
#include "gbp/terminal/worker.h"
#include "pin_config.h"
#include "storage/gbr_library.h"
#include "storage/gbr_store.h"

extern "C" {
extern char __end__;
extern char __StackLimit;
extern char __StackBottom;
extern char __StackTop;
void* _sbrk(int increment);
}

namespace app {
namespace {

constexpr std::uint32_t KEY_DEBOUNCE_MS = 25;
constexpr std::uint8_t KEY_RIGHT_MASK = 1u << 0;
constexpr std::uint8_t KEY_DOWN_MASK = 1u << 1;
constexpr std::uint8_t KEY_CONFIRM_MASK = 1u << 2;
constexpr std::uint8_t KEY_LEFT_MASK = 1u << 3;
constexpr std::uint8_t KEY_UP_MASK = 1u << 4;

gbp::terminal::Worker worker;
alignas(8) std::array<std::uint32_t, 1024> worker_stack{};

void run_worker() {
    // BTstack persists pairing data in flash. Register Core 1 as a flash
    // lockout victim before Worker::run publishes its Idle/ready state.
    if (!flash_safe_execute_core_init()) panic("Core 1 flash safety init failed");
    worker.run();
}

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

class KeyScanner {
  public:
    void initialize() {
        raw_pressed_ = read_pressed_mask();
        stable_pressed_ = raw_pressed_;
        raw_changed_at_ = get_absolute_time();
    }

    Key poll() {
        const absolute_time_t now = get_absolute_time();
        const std::uint8_t raw = read_pressed_mask();
        if (raw != raw_pressed_) {
            raw_pressed_ = raw;
            raw_changed_at_ = now;
        }
        if (raw_pressed_ == stable_pressed_ ||
            absolute_time_diff_us(raw_changed_at_, now) < static_cast<std::int64_t>(KEY_DEBOUNCE_MS) * 1000) {
            return Key::None;
        }
        const std::uint8_t pressed = raw_pressed_ & static_cast<std::uint8_t>(~stable_pressed_);
        stable_pressed_ = raw_pressed_;
        if (pressed & KEY_LEFT_MASK) return Key::Left;
        if (pressed & KEY_CONFIRM_MASK) return Key::Confirm;
        if (pressed & KEY_UP_MASK) return Key::Up;
        if (pressed & KEY_DOWN_MASK) return Key::Down;
        if (pressed & KEY_RIGHT_MASK) return Key::Right;
        return Key::None;
    }

  private:
    static std::uint8_t read_pressed_mask() {
        std::uint8_t mask = 0;
        if (!gpio_get(board::KEY_RIGHT_N)) mask |= KEY_RIGHT_MASK;
        if (!gpio_get(board::KEY_DOWN_N)) mask |= KEY_DOWN_MASK;
        if (!gpio_get(board::KEY_CONFIRM_N)) mask |= KEY_CONFIRM_MASK;
        if (!gpio_get(board::KEY_LEFT_N)) mask |= KEY_LEFT_MASK;
        if (!gpio_get(board::KEY_UP_N)) mask |= KEY_UP_MASK;
        return mask;
    }

    std::uint8_t raw_pressed_ = 0;
    std::uint8_t stable_pressed_ = 0;
    absolute_time_t raw_changed_at_{};
};

class Application {
  public:
    Application(const bool card_mounted, const config::FirmwareConfig& config)
        : card_mounted_(card_mounted), palette_key_(config.display.palette_key),
          backlight_percent_(config.display.backlight_percent),
          auto_cut_time_limit_us_(config.m1.auto_cut_time_limit_us),
          rx_sample_delay_us_(config.m2.rx_sample_delay_us), m3_config_(config.m3) {}

    [[noreturn]] void run() {
        storage_ready_ = card_mounted_ && store_.initialize();
        display::ui::initialize();
        display::init();
        display::set_palette_key(palette_key_);
        apply_initial_backlight();
        render();
        keys_.initialize();

        absolute_time_t next_sram_report = make_timeout_time_ms(1000);
        absolute_time_t next_m3_report = make_timeout_time_ms(2000);
        while (true) {
            if (bt_printer_.initialized()) {
                cyw43_arch_poll();
                bt_printer_.service();
            }
            if (time_reached(next_sram_report)) {
                report_sram_usage();
                next_sram_report = make_timeout_time_ms(1000);
            }
            bt::TraceEvent bt_event;
            while (bt_printer_.pop_trace(bt_event)) {
                printf("M3 BT event t=%lu %s a=0x%lx b=0x%lx\n",
                       static_cast<unsigned long>(bt_event.uptime_ms), bt::trace_kind_name(bt_event.kind),
                       static_cast<unsigned long>(bt_event.value), static_cast<unsigned long>(bt_event.extra));
            }
            if (state_.mode() == Mode::M3) {
                const auto connection = bt_printer_.connection_state();
                if (connection != drawn_connection_state_) {
                    if (drawn_connection_state_ == bt::ConnectionState::Connected &&
                        connection != bt::ConnectionState::Connected && state_.m3_page() == M3Page::Files) {
                        image_.clear();
                        state_.m3_connection_lost();
                    }
                    drawn_connection_state_ = connection;
                    dirty_ = true;
                }
            }
            if (time_reached(next_m3_report)) {
                if (state_.mode() == Mode::M3) {
                    printf("M3 alive t=%lu init=%u hci=%u link=%u status=%u page=%u/%u sent=%lu/%lu drops=%lu\n",
                           static_cast<unsigned long>(time_us_32() / 1000u),
                           bt_printer_.initialized() ? 1u : 0u, bt_printer_.hci_ready() ? 1u : 0u,
                           static_cast<unsigned>(bt_printer_.connection_state()),
                           static_cast<unsigned>(bt_printer_.status()), static_cast<unsigned>(state_.m3_page()),
                           static_cast<unsigned>(state_.m2_page()),
                           static_cast<unsigned long>(bt_printer_.progress()),
                           static_cast<unsigned long>(bt_printer_.total()),
                           static_cast<unsigned long>(bt_printer_.trace_drops()));
                }
                next_m3_report = make_timeout_time_ms(2000);
            }
            if (state_.mode() == Mode::M1) update_m1();
            if (state_.mode() == Mode::M2) update_m2();
            if (state_.mode() == Mode::M3) update_m3();
            handle_key(keys_.poll());
            render();
            tight_loop_contents();
        }
    }

  private:
    void ensure_worker_started() {
        if (worker_started_) return;
        // This lock is a no-op in polling mode, and keeps the Core 1 launch
        // safe if CYW43 is later changed back to background servicing.
        const bool bluetooth_active = bt_printer_.initialized();
        if (bluetooth_active) cyw43_arch_lwip_begin();
        multicore_launch_core1_with_stack(run_worker, worker_stack.data(), sizeof(worker_stack));
        while (worker.mode() != gbp::terminal::Worker::Mode::Idle) tight_loop_contents();
        worker_started_ = true;
        if (bluetooth_active) cyw43_arch_lwip_end();
    }

    void apply_initial_backlight() {
        for (std::size_t key = 0; key < display::BACKLIGHT_DUTY_PERCENT.size(); ++key) {
            if (display::BACKLIGHT_DUTY_PERCENT[key] == backlight_percent_) {
                display::set_backlight_key(static_cast<std::uint8_t>(key));
                return;
            }
        }
        display::set_backlight(static_cast<float>(backlight_percent_));
    }

    void adjust_brightness(const int delta) {
        if (delta < 0) {
            for (std::size_t index = display::BACKLIGHT_DUTY_PERCENT.size(); index > 0; --index) {
                const std::size_t key = index - 1;
                const auto percent = display::BACKLIGHT_DUTY_PERCENT[key];
                if (percent < backlight_percent_) {
                    display::set_backlight_key(static_cast<std::uint8_t>(key));
                    backlight_percent_ = percent;
                    return;
                }
            }
        } else {
            for (std::size_t key = 0; key < display::BACKLIGHT_DUTY_PERCENT.size(); ++key) {
                const auto percent = display::BACKLIGHT_DUTY_PERCENT[key];
                if (percent > backlight_percent_) {
                    display::set_backlight_key(static_cast<std::uint8_t>(key));
                    backlight_percent_ = percent;
                    return;
                }
            }
        }
    }

    void adjust_palette(const int delta) {
        const int next = static_cast<int>(palette_key_) + delta;
        if (next >= 0 && next < static_cast<int>(display::PALETTE_COUNT) &&
            display::set_palette_key(static_cast<std::uint8_t>(next))) {
            palette_key_ = static_cast<std::uint8_t>(next);
            dirty_ = true;
        }
    }

    void enter_m1() {
        if (!storage_ready_) {
            menu_error_ = "SD CARD ERROR";
            state_.menu_failed();
            dirty_ = true;
            return;
        }
        std::unique_ptr<gbp::terminal::M1State> next{
            new (std::nothrow) gbp::terminal::M1State{auto_cut_time_limit_us_}};
        if (!next) {
            printf("ERROR: M1 buffer allocation failed\n");
            menu_error_ = "M1 error";
            state_.menu_failed();
            dirty_ = true;
            return;
        }
        m1_ = std::move(next);
        drawn_m1_ = {};
        frame_owned_ = false;
        save_active_ = false;
        leaving_m1_ = false;
        confirmation_pending_ = false;
        ensure_worker_started();
        worker.start_receive(*m1_);
        state_.enter_m1();
        dirty_ = true;
    }

    void update_m1() {
        if (!frame_owned_ && m1_->acquire_frame(active_frame_)) {
            frame_owned_ = true;
            frame_displayed_or_skipped_ = false;
            save_active_ = store_.begin(active_frame_.data, active_frame_.size, active_frame_.print_control,
                                        active_frame_.append_to_previous);
            dirty_ = true;
        }

        const auto snapshot = m1_->snapshot();
        if (confirmation_pending_ &&
            (snapshot.view != gbp::terminal::ViewState::Preview ||
             snapshot.generation != confirmation_generation_)) {
            confirmation_pending_ = false;
        }
        if (frame_owned_ && (snapshot.view != gbp::terminal::ViewState::Preview ||
                             snapshot.generation != active_frame_.generation || confirmation_pending_ ||
                             leaving_m1_)) {
            frame_displayed_or_skipped_ = true;
        }
        if (snapshot.view != drawn_m1_.view ||
            (snapshot.view == gbp::terminal::ViewState::Receiving && snapshot.frames != drawn_m1_.frames) ||
            (snapshot.view == gbp::terminal::ViewState::Preview &&
             snapshot.generation != drawn_m1_.generation)) {
            dirty_ = true;
        }

        if (save_active_) {
            const auto result = store_.step();
            if (result == storage::GbrStore::StepResult::Complete ||
                result == storage::GbrStore::StepResult::Error) {
                save_active_ = false;
            }
        }
        if (frame_owned_ && !save_active_ && frame_displayed_or_skipped_) {
            m1_->release_frame();
            frame_owned_ = false;
        }
        if (leaving_m1_ && worker.mode() == gbp::terminal::Worker::Mode::Idle &&
            !frame_owned_ && !save_active_) {
            m1_.reset();
            leaving_m1_ = false;
            state_.exit_m1();
            dirty_ = true;
        }
    }

    void enter_m2() {
        const bool catalog_ok = card_mounted_ && catalog_.refresh();
        state_.enter_m2(catalog_ok ? catalog_.count() : 0);
        if (!catalog_ok) {
            error_text_ = card_mounted_ ? "GBR error" : "SD CARD ERROR";
            state_.file_failed();
        }
        dirty_ = true;
    }

    void enter_m3() {
        printf("M3 enter t=%lu ms\n", static_cast<unsigned long>(time_us_32() / 1000u));
        printf("M3 enter: bluetooth init begin\n");
        const bool bluetooth_ok = bt_printer_.initialize(m3_config_);
        printf("M3 enter: bluetooth init done ok=%u hci=%u\n", bluetooth_ok ? 1u : 0u,
               bt_printer_.hci_ready() ? 1u : 0u);
        state_.enter_m3();
        drawn_connection_state_ = bt_printer_.connection_state();
        dirty_ = true;
        printf("M3 enter: state ready page=%u\n", static_cast<unsigned>(state_.m3_page()));
    }

    void update_m2() {
        if (state_.m2_page() != M2Page::Printing) return;
        const auto progress = worker.print_progress();
        if (progress != drawn_print_progress_) dirty_ = true;
        const auto result = worker.print_result();
        if (result == gbp::host::PrintResult::None || worker.mode() != gbp::terminal::Worker::Mode::Idle) return;
        if (result == gbp::host::PrintResult::Success) {
            printed_bytes_ += image_.size();
            if (image_.block_index() + 1 < image_.block_count()) {
                if (image_.next_block()) {
                    start_m2_block();
                    dirty_ = true;
                    return;
                }
                printf("ERROR: GBR next block read failed\n");
                error_text_ = "GBR error";
                image_.clear();
                state_.print_finished(false);
                dirty_ = true;
                return;
            }
        }
        if (result != gbp::host::PrintResult::Success) {
            printf("ERROR: printer job failed: %s (%u), bytes sent=%lu/%lu\n",
                   gbp::host::print_result_name(result), static_cast<unsigned>(result),
                   static_cast<unsigned long>(printed_bytes_ + progress),
                   static_cast<unsigned long>(image_.total_size()));
            error_text_ = "Print error";
        }
        image_.clear();
        state_.print_finished(result == gbp::host::PrintResult::Success);
        dirty_ = true;
    }

    void update_m3() {
        if (state_.m3_page() != M3Page::Files || state_.m2_page() != M2Page::Printing) return;
        const auto status = bt_printer_.status();
        if (status == bt::PrintStatus::Printing) {
            if (bt_printer_.progress() != drawn_print_progress_) dirty_ = true;
            return;
        }
        if (status == bt::PrintStatus::Success) {
            if (image_.block_index() + 1 < image_.block_count()) {
                if (image_.next_block() &&
                    bt_printer_.start(image_.data(), image_.size(), image_.palette(), false,
                                      image_.block_index() + 1 == image_.block_count())) {
                    dirty_ = true;
                    return;
                }
                error_text_ = "GBR error";
                image_.clear();
                state_.print_finished(false);
            } else {
                image_.clear();
                state_.print_finished(true);
            }
        } else if (status == bt::PrintStatus::Error) {
            error_text_ = "BT print error";
            image_.clear();
            state_.print_finished(false);
        }
        dirty_ = true;
    }

    void handle_key(const Key key) {
        if (key == Key::None || leaving_m1_) return;
        const auto snapshot = m1_ ? m1_->snapshot() : gbp::terminal::M1State::Snapshot{};
        const Action action = state_.on_key(key, snapshot.view == gbp::terminal::ViewState::Waiting,
                                            snapshot.view == gbp::terminal::ViewState::Preview &&
                                                !confirmation_pending_, bt_printer_.connected());
        switch (action) {
            case Action::None: break;
            case Action::Redraw: dirty_ = true; break;
            case Action::BrightnessDown: adjust_brightness(-1); break;
            case Action::BrightnessUp: adjust_brightness(1); break;
            case Action::PaletteDown: adjust_palette(-1); break;
            case Action::PaletteUp: adjust_palette(1); break;
            case Action::EnterM1: enter_m1(); break;
            case Action::ExitM1:
                leaving_m1_ = true;
                worker.stop_receive();
                break;
            case Action::EnterM2: enter_m2(); break;
            case Action::EnterM3: enter_m3(); break;
            case Action::ExitM2:
                image_.clear();
                catalog_.clear();
                state_.exit_m2();
                dirty_ = true;
                break;
            case Action::ExitM3:
                bt_printer_.disconnect();
                image_.clear();
                catalog_.clear();
                state_.exit_m3();
                dirty_ = true;
                break;
            case Action::PairM3:
                bt_printer_.connect(true);
                dirty_ = true;
                break;
            case Action::DisconnectM3:
                bt_printer_.disconnect();
                dirty_ = true;
                break;
            case Action::EnterM3Files: {
                const bool catalog_ok = card_mounted_ && catalog_.refresh();
                state_.m3_files_ready(catalog_ok ? catalog_.count() : 0);
                if (!catalog_ok) {
                    error_text_ = card_mounted_ ? "GBR error" : "SD CARD ERROR";
                    state_.file_failed();
                }
                dirty_ = true;
                break;
            }
            case Action::ConfirmM1Preview:
                confirmation_pending_ = true;
                confirmation_generation_ = snapshot.generation;
                m1_->request_confirmation(snapshot.generation);
                break;
            case Action::OpenFile: {
                char filename[storage::GBR_NAME_CAPACITY]{};
                if (catalog_.name_at(state_.selected_file(), filename, sizeof(filename)) && image_.load(filename)) {
                    printf("M2 opened %s bytes=%lu\n", filename, static_cast<unsigned long>(image_.size()));
                    state_.file_opened(image_.block_count());
                } else {
                    error_text_ = "GBR error";
                    state_.file_failed();
                }
                dirty_ = true;
                break;
            }
            case Action::CloseFile:
                image_.clear();
                state_.file_closed();
                dirty_ = true;
                break;
            case Action::PreviousBlock:
            case Action::NextBlock:
                if (!image_.select_block(state_.selected_block())) {
                    printf("ERROR: GBR preview block %lu read failed\n",
                           static_cast<unsigned long>(state_.selected_block()));
                    image_.clear();
                    error_text_ = "GBR error";
                    state_.file_failed();
                }
                dirty_ = true;
                break;
            case Action::StartPrint:
                if (state_.mode() == Mode::M2) ensure_worker_started();
                if (state_.mode() == Mode::M3 || worker.mode() == gbp::terminal::Worker::Mode::Idle) {
                    if (image_.select_block(0)) {
                        if (state_.mode() == Mode::M3) {
                            if (bt_printer_.start(image_.data(), image_.size(), image_.palette(), true,
                                                  image_.block_count() == 1)) {
                                state_.print_started();
                                drawn_print_progress_ = 0;
                            } else {
                                error_text_ = "BT print error";
                                state_.print_finished(false);
                            }
                        } else {
                            printed_bytes_ = 0;
                            start_m2_block();
                            state_.print_started();
                            drawn_print_progress_ = 0;
                        }
                    } else {
                        printf("ERROR: GBR first block read failed\n");
                        image_.clear();
                        error_text_ = "GBR error";
                        state_.file_failed();
                    }
                    dirty_ = true;
                }
                break;
            case Action::CancelM3Print:
                bt_printer_.disconnect();
                image_.clear();
                state_.m3_connection_lost();
                dirty_ = true;
                break;
        }
    }

    bool draw_m1(const gbp::terminal::M1State::Snapshot& snapshot) {
        switch (snapshot.view) {
            case gbp::terminal::ViewState::Waiting: return gbp::terminal::draw_waiting();
            case gbp::terminal::ViewState::Receiving: return gbp::terminal::draw_receiving(snapshot.frames);
            case gbp::terminal::ViewState::Preview:
                if (confirmation_pending_) return false;
                if (frame_owned_ && active_frame_.generation == snapshot.generation) {
                    gbp::terminal::draw_preview(active_frame_.data, active_frame_.size, active_frame_.palette);
                    return true;
                }
                return drawn_mode_ == Mode::M1 && drawn_m1_.view == gbp::terminal::ViewState::Preview &&
                       drawn_m1_.generation == snapshot.generation;
        }
        return false;
    }

    bool draw_screen(const gbp::terminal::M1State::Snapshot& snapshot) {
        switch (state_.mode()) {
            case Mode::Menu:
                return state_.menu_page() == MenuPage::Error ? screen::draw_error(menu_error_) :
                    screen::draw_menu(state_.menu_selection());
            case Mode::M1: return draw_m1(snapshot);
            case Mode::M2:
                switch (state_.m2_page()) {
                    case M2Page::List: return screen::draw_file_list(catalog_, state_.selected_file());
                    case M2Page::Preview:
                        gbp::terminal::draw_preview(image_.data(), image_.size(),
                                                    gbr::format::preview_palette(image_.palette()));
                        return screen::draw_block_indicator(image_.block_index(), image_.block_count());
                    case M2Page::Printing:
                        return screen::draw_printing(printed_bytes_ + worker.print_progress(), image_.total_size());
                    case M2Page::Error: return screen::draw_error(error_text_);
                }
            case Mode::M3:
                switch (state_.m3_page()) {
                    case M3Page::Menu:
                        return screen::draw_m3_menu(state_.m3_selection(), bt_printer_.connected());
                    case M3Page::Pairing: {
                        std::string_view status = "Disconnected";
                        switch (bt_printer_.connection_state()) {
                            case bt::ConnectionState::Disconnected: break;
                            case bt::ConnectionState::Connecting: status = "Connecting"; break;
                            case bt::ConnectionState::Connected: status = "Connected"; break;
                            case bt::ConnectionState::Error: status = "Pair failed"; break;
                        }
                        return screen::draw_m3_pairing(state_.m3_pair_selection(), status);
                    }
                    case M3Page::Files:
                        switch (state_.m2_page()) {
                            case M2Page::List: return screen::draw_file_list(catalog_, state_.selected_file(), true);
                            case M2Page::Preview:
                                gbp::terminal::draw_preview(image_.data(), image_.size(),
                                                            gbr::format::preview_palette(image_.palette()));
                                return screen::draw_block_indicator(image_.block_index(), image_.block_count());
                            case M2Page::Printing:
                                return screen::draw_printing(bt_printer_.progress(), bt_printer_.total(), true);
                            case M2Page::Error: return screen::draw_error(error_text_);
                        }
                }
        }
        return false;
    }

    void render() {
        if (!dirty_ || display::is_presenting()) return;
        const bool entering_m3_display = drawn_mode_ != Mode::M3 && state_.mode() == Mode::M3;
        const auto snapshot = m1_ ? m1_->snapshot() : gbp::terminal::M1State::Snapshot{};
        if (!draw_screen(snapshot)) return;
        if (state_.mode() == Mode::M3 && bt_printer_.connected() && !screen::draw_paired_badge()) return;
        if (!display::present()) return;
        drawn_mode_ = state_.mode();
        if (drawn_mode_ == Mode::M1) {
            drawn_m1_ = snapshot;
            if (snapshot.view == gbp::terminal::ViewState::Preview && frame_owned_ &&
                active_frame_.generation == snapshot.generation) {
                frame_displayed_or_skipped_ = true;
            }
        }
        if (drawn_mode_ == Mode::M2 && state_.m2_page() == M2Page::Printing) {
            drawn_print_progress_ = worker.print_progress();
        }
        if (drawn_mode_ == Mode::M3 && state_.m3_page() == M3Page::Files &&
            state_.m2_page() == M2Page::Printing) {
            drawn_print_progress_ = bt_printer_.progress();
        }
        dirty_ = false;
        if (entering_m3_display) printf("M3 screen submitted page=%u\n", static_cast<unsigned>(state_.m2_page()));
    }

    void start_m2_block() {
        const auto control = gbr::format::m2_print_control(image_.print_control());
        printf("M2 block %lu/%lu: recorded sheets=%u, sending sheets=1\n",
               static_cast<unsigned long>(image_.block_index() + 1),
               static_cast<unsigned long>(image_.block_count()),
               static_cast<unsigned>(image_.print_control()[0]));
        worker.start_print(image_.data(), image_.size(), control, rx_sample_delay_us_);
    }

    bool card_mounted_ = false;
    bool storage_ready_ = false;
    bool worker_started_ = false;
    std::uint8_t palette_key_ = display::WHITE_PALETTE_KEY;
    std::uint8_t backlight_percent_ = 50;
    std::uint32_t auto_cut_time_limit_us_ = 1'000'000;
    std::uint8_t rx_sample_delay_us_ = gbp::host::RX_SAMPLE_DELAY_DEFAULT_US;
    config::M3Config m3_config_{};
    bt::BluetoothPrinter bt_printer_{};
    bt::ConnectionState drawn_connection_state_ = bt::ConnectionState::Disconnected;
    StateMachine state_{};
    KeyScanner keys_{};
    storage::GbrStore store_{};
    storage::GbrCatalog catalog_{};
    storage::GbrImage image_{};
    std::unique_ptr<gbp::terminal::M1State> m1_{};
    gbp::terminal::M1State::Frame active_frame_{};
    gbp::terminal::M1State::Snapshot drawn_m1_{};
    Mode drawn_mode_ = Mode::Menu;
    std::string_view menu_error_ = "SD CARD ERROR";
    std::string_view error_text_ = "GBR error";
    std::size_t drawn_print_progress_ = 0;
    std::size_t printed_bytes_ = 0;
    std::uint32_t confirmation_generation_ = 0;
    bool frame_owned_ = false;
    bool frame_displayed_or_skipped_ = false;
    bool save_active_ = false;
    bool leaving_m1_ = false;
    bool confirmation_pending_ = false;
    bool dirty_ = true;
};

}  // namespace

void run(const bool card_mounted, const config::FirmwareConfig& firmware_config) {
    // The M3 raster and BT buffers are large enough to keep the application
    // object out of core 0's limited call stack.
    static Application application(card_mounted, firmware_config);
    application.run();
}

}  // namespace app
