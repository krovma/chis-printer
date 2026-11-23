// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/application.h"

#include <array>
#include <cstdio>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include "types.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "app/screen.h"
#include "app/settings.h"
#include "app/state_machine.h"
#include "btprinter/bluetooth_printer.h"
#include "btprinter/print_layout.h"
#include "config/variables.h"
#include "display/ui/screen.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"
#include "gbr/format.h"
#include "gbp/terminal/m1_state.h"
#include "gbp/terminal/screen.h"
#include "gbp/terminal/worker.h"
#include "pin_config.h"
#include "storage/gbr_library.h"
#include "storage/gbr_store.h"
#include "storage/sd_card.h"

extern "C" {
extern char __end__;
extern char __StackLimit;
extern char __StackBottom;
extern char __StackTop;
void* _sbrk(int increment);
}

namespace App {
namespace {

constexpr uint32 KEY_DEBOUNCE_MS = 25;
constexpr uint8 KEY_RIGHT_MASK = 1u << 0;
constexpr uint8 KEY_DOWN_MASK = 1u << 1;
constexpr uint8 KEY_CONFIRM_MASK = 1u << 2;
constexpr uint8 KEY_LEFT_MASK = 1u << 3;
constexpr uint8 KEY_UP_MASK = 1u << 4;

Gbp::Terminal::Worker worker;
alignas(8) std::array<uint32, 1024> worker_stack{};

void run_worker() {
    // BTstack persists pairing data in flash. Register Core 1 as a flash
    // lockout victim before Worker::run publishes its Idle/ready state.
    if (!flash_safe_execute_core_init()) {
        panic("Core 1 flash safety init failed");
    }
    worker.run();
}

void report_sram_usage() {
    const auto ram_start = static_cast<uintptr>(SRAM_BASE);
    const auto ram_end = reinterpret_cast<uintptr>(&__StackLimit);
    const auto static_end = reinterpret_cast<uintptr>(&__end__);
    const auto heap_end = reinterpret_cast<uintptr>(_sbrk(0));
    const auto stack_bottom = reinterpret_cast<uintptr>(&__StackBottom);
    const auto stack_top = reinterpret_cast<uintptr>(&__StackTop);
    uintptr stack_pointer = 0;
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
        const uint8 raw = read_pressed_mask();
        if (raw != raw_pressed_) {
            raw_pressed_ = raw;
            raw_changed_at_ = now;
        }
        if (raw_pressed_ == stable_pressed_ ||
            absolute_time_diff_us(raw_changed_at_, now) < static_cast<int64>(KEY_DEBOUNCE_MS) * 1000) {
            return Key::None;
        }
        const uint8 pressed = raw_pressed_ & static_cast<uint8>(~stable_pressed_);
        stable_pressed_ = raw_pressed_;
        if (pressed & KEY_LEFT_MASK) {
            return Key::Left;
        }
        if (pressed & KEY_CONFIRM_MASK) {
            return Key::Confirm;
        }
        if (pressed & KEY_UP_MASK) {
            return Key::Up;
        }
        if (pressed & KEY_DOWN_MASK) {
            return Key::Down;
        }
        if (pressed & KEY_RIGHT_MASK) {
            return Key::Right;
        }
        return Key::None;
    }

  private:
    static uint8 read_pressed_mask() {
        uint8 mask = 0;
        if (!gpio_get(Board::KEY_RIGHT_N)) {
            mask |= KEY_RIGHT_MASK;
        }
        if (!gpio_get(Board::KEY_DOWN_N)) {
            mask |= KEY_DOWN_MASK;
        }
        if (!gpio_get(Board::KEY_CONFIRM_N)) {
            mask |= KEY_CONFIRM_MASK;
        }
        if (!gpio_get(Board::KEY_LEFT_N)) {
            mask |= KEY_LEFT_MASK;
        }
        if (!gpio_get(Board::KEY_UP_N)) {
            mask |= KEY_UP_MASK;
        }
        return mask;
    }

    uint8 raw_pressed_ = 0;
    uint8 stable_pressed_ = 0;
    absolute_time_t raw_changed_at_{};
};

class Application {
  public:
    explicit Application(const bool card_mounted) : card_mounted_(card_mounted) {}

    [[noreturn]] void run() {
        storage_ready_ = card_mounted_ && store_.initialize();
        Display::Ui::initialize(Config::Vars::locale.get());
        Display::init();
        Display::set_palette_key(Config::Vars::palette.get());
        apply_initial_backlight();
        render();
        keys_.initialize();

        absolute_time_t next_sram_report = make_timeout_time_ms(1000);
#if defined(KP_BT_DEBUG)
        absolute_time_t next_m3_report = make_timeout_time_ms(2000);
#endif
        while (true) {
            // Keep servicing the Bluetooth link across mode changes.
            if (bt_printer_.initialized()) {
                cyw43_arch_poll();
                bt_printer_.service();
            }
            if (time_reached(next_sram_report)) {
                report_sram_usage();
                next_sram_report = make_timeout_time_ms(1000);
            }
            BtPrinter::TraceEvent bt_event;
            while (bt_printer_.pop_trace(bt_event)) {
                printf("M3 BT event t=%lu %s a=0x%lx b=0x%lx\n",
                       static_cast<unsigned long>(bt_event.uptime_ms), BtPrinter::trace_kind_name(bt_event.kind),
                       static_cast<unsigned long>(bt_event.value), static_cast<unsigned long>(bt_event.extra));
            }
            if (state_.mode() == Mode::M3 || state_.mode() == Mode::M13) {
                const auto connection = bt_printer_.connection_state();
                if (connection != drawn_connection_state_) {
                    if (drawn_connection_state_ == BtPrinter::ConnectionState::Connected &&
                        connection != BtPrinter::ConnectionState::Connected && state_.mode() == Mode::M3 &&
                        state_.m3_page() == M3Page::Files) {
                        image_.clear();
                        state_.m3_connection_lost();
                    }
                    drawn_connection_state_ = connection;
                    dirty_ = true;
                }
            }
#if defined(KP_BT_DEBUG)
            if (time_reached(next_m3_report)) {
                if (state_.mode() == Mode::M3 || state_.mode() == Mode::M13) {
                    printf("%s alive t=%lu init=%u hci=%u link=%u status=%u page=%u/%u sent=%lu/%lu drops=%lu\n",
                           state_.mode() == Mode::M13 ? "M1+3" : "M3",
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
#endif
            if (state_.mode() == Mode::M1) {
                update_m1();
            }
            if (state_.mode() == Mode::M2) {
                update_m2();
            }
            if (state_.mode() == Mode::M3) {
                update_m3();
            }
            if (state_.mode() == Mode::M13 && state_.m3_page() == M3Page::Receive) {
                update_m13();
            }
            handle_key(keys_.poll());
            render();
            tight_loop_contents();
        }
    }

  private:
    void ensure_worker_started() {
        if (worker_started_) {
            return;
        }
        // This lock is a no-op in polling mode, and keeps the Core 1 launch
        // safe if CYW43 is later changed back to background servicing.
        const bool bluetooth_active = bt_printer_.initialized();
        if (bluetooth_active) {
            cyw43_arch_lwip_begin();
        }
        multicore_launch_core1_with_stack(run_worker, worker_stack.data(), sizeof(worker_stack));
        while (worker.mode() != Gbp::Terminal::Worker::Mode::Idle) {
            tight_loop_contents();
        }
        worker_started_ = true;
        if (bluetooth_active) {
            cyw43_arch_lwip_end();
        }
    }

    void apply_initial_backlight() {
        for (usize key = 0; key < Display::BACKLIGHT_DUTY_PERCENT.size(); ++key) {
            if (Display::BACKLIGHT_DUTY_PERCENT[key] == Config::Vars::backlight_percent.get()) {
                Display::set_backlight_key(static_cast<uint8>(key));
                return;
            }
        }
        Display::set_backlight(static_cast<float>(Config::Vars::backlight_percent.get()));
    }

    void adjust_palette(const int delta) {
        const int next = static_cast<int>(Config::Vars::palette.get()) + delta;
        if (next >= 0 && next < static_cast<int>(Display::PALETTE_COUNT) &&
            Config::Vars::palette.set(static_cast<uint8>(next)) &&
            Display::set_palette_key(Config::Vars::palette.get())) {
            dirty_ = true;
        }
    }

    void preview_setting(const Setting setting) {
        switch (setting) {
            case Setting::Locale: Display::Ui::set_locale(Config::Vars::locale.get()); break;
            case Setting::Palette: Display::set_palette_key(Config::Vars::palette.get()); break;
            case Setting::Backlight:
                Display::set_backlight(static_cast<float>(Config::Vars::backlight_percent.get()));
                break;
            default: break;
        }
    }

    void begin_setting() {
        setting_original_ = setting_value(state_.setting_selection());
        settings_error_ = {};
        dirty_ = true;
    }

    void cancel_setting() {
        const Setting setting = state_.setting_selection();
        set_setting_value(setting, setting_original_, Config::WriteMode::RuntimeOnly);
        preview_setting(setting);
        settings_error_ = {};
        dirty_ = true;
    }

    void adjust_setting(const int delta) {
        const Setting setting = state_.setting_selection();
        const uint32 next = stepped_setting_value(setting, setting_value(setting), delta);
        if (!set_setting_value(setting, next, Config::WriteMode::RuntimeOnly)) return;
        preview_setting(setting);
        settings_error_ = {};
        dirty_ = true;
    }

    void reset_left_margin() {
        if (state_.setting_selection() != Setting::LeftMargin) return;
        const auto advance = Config::Vars::current_m3_config().resolved_advance();
        const uint32 centered_dots = Config::default_left_margin_dots(advance.printable_width_dots,
                                                                       advance.image_scale_quarters);
        if (!set_setting_value(Setting::LeftMargin, centered_dots, Config::WriteMode::RuntimeOnly)) return;
        preview_setting(Setting::LeftMargin);
        settings_error_ = {};
        dirty_ = true;
    }

    void save_setting() {
        const Setting setting = state_.setting_selection();
        if (!set_setting_value(setting, setting_value(setting), Config::WriteMode::RuntimeAndToml)) {
            settings_error_ = card_mounted_ ? "settings_save_failed" : "settings_sd_required";
            dirty_ = true;
            return;
        }
        settings_error_ = {};
        state_.setting_saved();
        dirty_ = true;
    }

    void enter_m1() {
        if (!storage_ready_) {
            menu_error_ = "SD CARD ERROR";
            state_.menu_failed();
            dirty_ = true;
            return;
        }
        std::unique_ptr<Gbp::Terminal::M1State> next{
            new (std::nothrow) Gbp::Terminal::M1State{Config::Vars::auto_cut_time_limit_us.get()}};
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
        worker.start_receive(*m1_, false, Config::Vars::simulated_print_time_us.get());
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
            (snapshot.view != Gbp::Terminal::ViewState::Preview || snapshot.generation != confirmation_generation_)) {
            confirmation_pending_ = false;
        }
        if (frame_owned_ && (snapshot.view != Gbp::Terminal::ViewState::Preview ||
                             snapshot.generation != active_frame_.generation || confirmation_pending_ || leaving_m1_)) {
            frame_displayed_or_skipped_ = true;
        }
        if (snapshot.view != drawn_m1_.view ||
            (snapshot.view == Gbp::Terminal::ViewState::Receiving && snapshot.frames != drawn_m1_.frames) ||
            (snapshot.view == Gbp::Terminal::ViewState::Preview && snapshot.generation != drawn_m1_.generation)) {
            dirty_ = true;
        }

        if (save_active_) {
            const auto result = store_.step();
            if (result == Storage::GbrStore::StepResult::Complete || result == Storage::GbrStore::StepResult::Error) {
                save_active_ = false;
            }
        }
        if (frame_owned_ && !save_active_ && frame_displayed_or_skipped_) {
            m1_->release_frame();
            frame_owned_ = false;
        }
        if (leaving_m1_ && worker.mode() == Gbp::Terminal::Worker::Mode::Idle && !frame_owned_ && !save_active_) {
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

    void enter_bluetooth_mode(const Mode mode) {
        const bool m13 = mode == Mode::M13;
        const char* mode_name = m13 ? "M1+3" : "M3";
        const auto config = Config::Vars::current_m3_config();
        if (!bt_printer_.allocate_print_buffers(config)) {
            menu_error_ = m13 ? "M1+3 error" : "BT print error";
            state_.menu_failed();
            dirty_ = true;
            return;
        }
#if defined(KP_BT_DEBUG)
        printf("%s enter t=%lu ms: bluetooth init begin\n", mode_name,
               static_cast<unsigned long>(time_us_32() / 1000u));
#endif
        const bool bluetooth_ok = bt_printer_.initialize(config);
#if defined(KP_BT_DEBUG)
        printf("%s enter: bluetooth init done ok=%u hci=%u\n", mode_name, bluetooth_ok ? 1u : 0u,
               bt_printer_.hci_ready() ? 1u : 0u);
#endif
        if (!bluetooth_ok) {
            printf("ERROR: %s Bluetooth init failed\n", mode_name);
        }
        if (m13) {
            state_.enter_m13();
        } else {
            state_.enter_m3();
        }
        drawn_connection_state_ = bt_printer_.connection_state();
        dirty_ = true;
    }

    void exit_bluetooth_mode() {
        bt_printer_.release_print_buffers();
        image_.clear();
        catalog_.clear();
        if (state_.mode() == Mode::M13) {
            state_.exit_m13();
        } else {
            state_.exit_m3();
        }
        dirty_ = true;
    }

    bool prepare_bt_layout() {
        label_layout_ = {};
        if (Config::Vars::paper_type.get() != Config::PaperType::Label) return true;
        label_layout_ = BtPrinter::layout_label(image_.source_rows(), image_.inner_margin_rows(),
                                                Config::Vars::current_m3_config().resolved_advance().image_scale_quarters,
                                                Config::Vars::label_length_mm.get());
        return !label_layout_.overflow;
    }

    bool start_bt_block(const bool first, const usize previous_after_rows = 0) {
        const bool last = image_.block_index() + 1 == image_.block_count();
        const uint8 scale_quarters = Config::Vars::current_m3_config().resolved_advance().image_scale_quarters;
        const usize current_before_rows = static_cast<usize>(image_.print_control()[1] >> 4) * 8;
        const usize current_after_rows = static_cast<usize>(image_.print_control()[1] & 0x0f) * 8;
        // Keep GBR margins on roll paper; label paper replaces only the outer margins.
        usize before_dots = Config::scaled_dots(previous_after_rows + current_before_rows, scale_quarters);
        usize after_dots = last ? Config::scaled_dots(current_after_rows, scale_quarters) : 0;
        if (Config::Vars::paper_type.get() == Config::PaperType::Label) {
            before_dots = first ? label_layout_.top_dots :
                Config::scaled_dots(previous_after_rows + current_before_rows, scale_quarters);
            after_dots = last ? label_layout_.bottom_dots +
                BtPrinter::label_length_dots(Config::Vars::label_gap_mm.get()) : 0;
        }
        return bt_printer_.start(image_.data(), image_.size(), image_.palette(),
                                 first, last, before_dots, after_dots);
    }

    bool select_first_print_block() {
        if (image_.select_block(0)) return true;
        printf("ERROR: GBR first block read failed\n");
        image_.clear();
        error_text_ = "GBR error";
        state_.file_failed();
        return false;
    }

    void start_m3_print() {
        if (!select_first_print_block()) return;
        if (start_bt_block(true)) {
            state_.print_started();
            drawn_print_progress_ = 0;
        } else {
            error_text_ = "BT print error";
            state_.print_finished(false);
        }
    }

    void enter_m13_receive() {
        if (!storage_ready_) {
            menu_error_ = "SD CARD ERROR";
            exit_bluetooth_mode();
            state_.menu_failed();
            return;
        }
        std::unique_ptr<Gbp::Terminal::M1State> next{
            new (std::nothrow) Gbp::Terminal::M1State{Config::Vars::auto_cut_time_limit_us.get()}};
        if (!next) {
            printf("ERROR: M1+3 buffer allocation failed\n");
            menu_error_ = "M1+3 error";
            exit_bluetooth_mode();
            state_.menu_failed();
            return;
        }
        m1_ = std::move(next);
        active_frame_ = {};
        frame_owned_ = false;
        save_active_ = false;
        confirmation_pending_ = false;
        m13_cut_seen_ = m1_->cut_sequence();
        m13_discarded_seen_ = 0;
        m13_cut_pending_ = false;
        m13_group_saved_ = false;
        m13_save_failed_ = false;
        m13_printing_ = false;
        m13_stop_requested_ = false;
        m13_manual_exit_requested_ = false;
        m13_error_ = {};
        drawn_m13_ = {};
        drawn_m13_tenths_ = UINT32_MAX;
        ensure_worker_started();
        worker.start_receive(*m1_, true, Config::Vars::simulated_print_time_us.get());
        state_.m13_receive_started();
        printf("M1+3 receive started\n");
        dirty_ = true;
    }

    void leave_m13_receive(const bool pairing = true) {
        if (worker.mode() != Gbp::Terminal::Worker::Mode::Idle || frame_owned_ || save_active_ || store_.busy()) {
            printf("ERROR: M1+3 exit deferred: worker=%u frame=%u sd=%u\n",
                   static_cast<unsigned>(worker.mode()), frame_owned_ ? 1u : 0u, store_.busy() ? 1u : 0u);
            return;
        }
        image_.clear();
        m1_.reset();
        m13_cut_pending_ = false;
        m13_group_saved_ = false;
        m13_printing_ = false;
        m13_stop_requested_ = false;
        m13_manual_exit_requested_ = false;
        if (pairing) {
            state_.m3_connection_lost();
        } else {
            state_.m13_receive_finished();
        }
        printf("M1+3 receive stopped; %s menu\n", pairing ? "pairing" : "mode");
        dirty_ = true;
    }

    void finish_m13_print(const bool success) {
        image_.clear();
        m13_printing_ = false;
        m13_cut_pending_ = false;
        m13_group_saved_ = false;
        m13_save_failed_ = false;
        if (!success && m13_error_.empty()) {
            m13_error_ = "BT print error";
        }
        if (bt_printer_.connected()) {
            if (m13_manual_exit_requested_) {
                leave_m13_receive(false);
                return;
            }
            worker.start_receive(*m1_, true, Config::Vars::simulated_print_time_us.get());
            printf("M1+3 receive resumed after BT print ok=%u\n", success ? 1u : 0u);
        } else {
            leave_m13_receive();
            return;
        }
        dirty_ = true;
    }

    void update_m13() {
        if (!frame_owned_ && m1_->acquire_frame(active_frame_)) {
            frame_owned_ = true;
            m1_->request_confirmation(active_frame_.generation);
            save_active_ = store_.begin(active_frame_.data, active_frame_.size, active_frame_.print_control,
                                        active_frame_.append_to_previous);
            if (!save_active_) {
                m13_save_failed_ = true;
                m13_error_ = "GBR save error";
            } else if (!m13_save_failed_) {
                m13_error_ = {};
            }
            dirty_ = true;
        }

        const auto snapshot = m1_->snapshot(time_us_32());
        if (snapshot.view != drawn_m13_.view ||
            (snapshot.view == Gbp::Terminal::ViewState::Receiving && snapshot.frames != drawn_m13_.frames)) {
            dirty_ = true;
        }
        if (snapshot.discarded_frames != m13_discarded_seen_) {
            m13_discarded_seen_ = snapshot.discarded_frames;
            m13_save_failed_ = true;
            m13_error_ = "GBR save error";
            printf("ERROR: M1+3 frame discarded before SD save\n");
            dirty_ = true;
        }
        const uint32 tenths = snapshot.cut_pending ?
            (snapshot.cut_remaining_us + 99'999u) / 100'000u : UINT32_MAX;
        if (tenths != drawn_m13_tenths_) {
            dirty_ = true;
        }
        if (snapshot.cut_sequence != m13_cut_seen_) {
            m13_cut_seen_ = snapshot.cut_sequence;
            m13_cut_pending_ = true;
            printf("M1+3 AutoCut expired; waiting for SD and Core1 pause\n");
            dirty_ = true;
        }

        if (save_active_) {
            const auto result = store_.step();
            if (result == Storage::GbrStore::StepResult::Complete) {
                save_active_ = false;
                m13_group_saved_ = true;
            } else if (result == Storage::GbrStore::StepResult::Error) {
                save_active_ = false;
                m13_save_failed_ = true;
                m13_error_ = "GBR save error";
                dirty_ = true;
            }
        }
        if (frame_owned_ && !save_active_) {
            m1_->release_frame();
            frame_owned_ = false;
        }

        if (m13_printing_) {
            const auto status = bt_printer_.status();
            if (status == BtPrinter::PrintStatus::Printing && bt_printer_.connected()) {
                return;
            }
            if (status == BtPrinter::PrintStatus::Success) {
                if (image_.block_index() + 1 < image_.block_count()) {
                    if (!bt_printer_.connected()) {
                        finish_m13_print(false);
                        return;
                    }
                    const usize previous_after_rows = static_cast<usize>(image_.print_control()[1] & 0x0f) * 8;
                    if (!image_.next_block()) {
                        m13_error_ = "GBR read error";
                        printf("ERROR: M1+3 next GBR block read failed\n");
                        finish_m13_print(false);
                        return;
                    }
                    if (start_bt_block(false, previous_after_rows)) {
                        printf("M1+3 BT block %lu/%lu\n",
                               static_cast<unsigned long>(image_.block_index() + 1),
                               static_cast<unsigned long>(image_.block_count()));
                        return;
                    }
                    m13_error_ = "BT print error";
                    printf("ERROR: M1+3 next BT block start failed\n");
                    finish_m13_print(false);
                    return;
                }
                printf("M1+3 BT print completed\n");
                finish_m13_print(true);
                return;
            }
            printf("ERROR: M1+3 BT print interrupted status=%u\n", static_cast<unsigned>(status));
            finish_m13_print(false);
            return;
        }

        if (m13_cut_pending_ && worker.mode() == Gbp::Terminal::Worker::Mode::Idle && !frame_owned_ && !save_active_) {
            if (!bt_printer_.connected()) {
                leave_m13_receive();
                return;
            }
            if (m13_save_failed_ || !m13_group_saved_) {
                printf("M1+3 AutoCut skipped: saved=%u error=%u\n",
                       m13_group_saved_ ? 1u : 0u, m13_save_failed_ ? 1u : 0u);
                m13_cut_pending_ = false;
                m13_group_saved_ = false;
                m13_save_failed_ = false;
                if (m13_manual_exit_requested_) {
                    leave_m13_receive(false);
                    return;
                }
                worker.start_receive(*m1_, true, Config::Vars::simulated_print_time_us.get());
                dirty_ = true;
                return;
            }
            char filename[Storage::GBR_NAME_CAPACITY]{};
            if (!store_.last_filename(filename, sizeof(filename)) || !image_.load(filename)) {
                printf("ERROR: M1+3 could not load saved GBR %s\n", filename);
                m13_error_ = "GBR read error";
                finish_m13_print(false);
                return;
            }
            if (!prepare_bt_layout()) {
                printf("M1+3 label content too long: %lu/%lu dots; GBR saved as %s\n",
                       static_cast<unsigned long>(label_layout_.content_dots),
                       static_cast<unsigned long>(label_layout_.label_dots), filename);
                m13_error_ = "Label too long";
                finish_m13_print(false);
                return;
            }
            if (!start_bt_block(true)) {
                printf("ERROR: M1+3 could not start BT print for %s\n", filename);
                m13_error_ = "BT print error";
                finish_m13_print(false);
                return;
            }
            m13_printing_ = true;
            printf("M1+3 BT print started %s blocks=%lu\n", filename,
                   static_cast<unsigned long>(image_.block_count()));
            report_sram_usage();
            dirty_ = true;
            return;
        }

        if (bt_printer_.connected() && m13_stop_requested_ && !m13_manual_exit_requested_ &&
            worker.mode() == Gbp::Terminal::Worker::Mode::Idle && !frame_owned_ && !save_active_) {
            worker.start_receive(*m1_, true, Config::Vars::simulated_print_time_us.get());
            m13_stop_requested_ = false;
            printf("M1+3 link recovered before pairing menu; receive resumed\n");
            dirty_ = true;
            return;
        }

        if (!bt_printer_.connected() && !m13_printing_) {
            if (worker.mode() == Gbp::Terminal::Worker::Mode::Idle && !frame_owned_ && !save_active_) {
                leave_m13_receive();
            } else if (!m13_stop_requested_ && worker.mode() == Gbp::Terminal::Worker::Mode::Receiving &&
                       snapshot.view == Gbp::Terminal::ViewState::Waiting && !snapshot.cut_pending &&
                       !m13_group_saved_ && !frame_owned_ && !save_active_) {
                worker.stop_receive();
                m13_stop_requested_ = true;
                printf("M1+3 disconnected while idle; stopping Core1\n");
            }
        } else if (m13_manual_exit_requested_ && !m13_printing_ && !m13_cut_pending_) {
            if (worker.mode() == Gbp::Terminal::Worker::Mode::Idle && !frame_owned_ && !save_active_ &&
                !m13_group_saved_) {
                leave_m13_receive(false);
            } else if (!m13_stop_requested_ && worker.mode() == Gbp::Terminal::Worker::Mode::Receiving &&
                       !snapshot.cut_pending && !m13_group_saved_ && !frame_owned_ && !save_active_) {
                worker.stop_receive();
                m13_stop_requested_ = true;
                printf("M1+3 manual exit; stopping Core1\n");
            }
        }
    }

    void update_m2() {
        if (state_.m2_page() != M2Page::Printing) {
            return;
        }
        const auto progress = worker.print_progress();
        if (progress != drawn_print_progress_) {
            dirty_ = true;
        }
        const auto result = worker.print_result();
        if (result == Gbp::Host::PrintResult::None || worker.mode() != Gbp::Terminal::Worker::Mode::Idle) {
            return;
        }
        if (result == Gbp::Host::PrintResult::Success) {
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
        if (result != Gbp::Host::PrintResult::Success) {
            printf("ERROR: printer job failed: %s (%u), bytes sent=%lu/%lu\n",
                   Gbp::Host::print_result_name(result), static_cast<unsigned>(result),
                   static_cast<unsigned long>(printed_bytes_ + progress),
                   static_cast<unsigned long>(image_.total_size()));
            error_text_ = "Print error";
        }
        image_.clear();
        state_.print_finished(result == Gbp::Host::PrintResult::Success);
        dirty_ = true;
    }

    void update_m3() {
        if (state_.m3_page() != M3Page::Files || state_.m2_page() != M2Page::Printing) {
            return;
        }
        const auto status = bt_printer_.status();
        if (status == BtPrinter::PrintStatus::Printing) {
            if (bt_printer_.progress() != drawn_print_progress_) {
                dirty_ = true;
            }
            return;
        }
        if (status == BtPrinter::PrintStatus::Success) {
            if (image_.block_index() + 1 < image_.block_count()) {
                const usize previous_after_rows = static_cast<usize>(image_.print_control()[1] & 0x0f) * 8;
                if (image_.next_block() && start_bt_block(false, previous_after_rows)) {
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
        } else if (status == BtPrinter::PrintStatus::Error) {
            error_text_ = "BT print error";
            image_.clear();
            state_.print_finished(false);
        }
        dirty_ = true;
    }

    void handle_key(const Key key) {
        if (key == Key::None || leaving_m1_) {
            return;
        }
        const auto snapshot = m1_ ? m1_->snapshot() : Gbp::Terminal::M1State::Snapshot{};
        const Action action = state_.on_key(key, snapshot.view == Gbp::Terminal::ViewState::Waiting,
                                            snapshot.view == Gbp::Terminal::ViewState::Preview,
                                            bt_printer_.connected(),
                                            Config::Vars::centering.get() == Config::CenteringMode::Firmware);
        switch (action) {
            case Action::None: break;
            case Action::Redraw: dirty_ = true; break;
            case Action::PaletteDown: adjust_palette(-1); break;
            case Action::PaletteUp: adjust_palette(1); break;
            case Action::EnterM1: enter_m1(); break;
            case Action::ExitM1:
                leaving_m1_ = true;
                worker.stop_receive();
                break;
            case Action::EnterM2: enter_m2(); break;
            case Action::EnterM3: enter_bluetooth_mode(Mode::M3); break;
            case Action::EnterM13: enter_bluetooth_mode(Mode::M13); break;
            case Action::EnterSettings:
                state_.enter_settings();
                dirty_ = true;
                break;
            case Action::ExitSettings:
                state_.exit_settings();
                dirty_ = true;
                break;
            case Action::BeginSetting: begin_setting(); break;
            case Action::BeginAndResetLeftMargin:
                begin_setting();
                reset_left_margin();
                break;
            case Action::CancelSetting: cancel_setting(); break;
            case Action::DecreaseSetting: adjust_setting(-1); break;
            case Action::IncreaseSetting: adjust_setting(1); break;
            case Action::ResetLeftMargin: reset_left_margin(); break;
            case Action::SaveSetting: save_setting(); break;
            case Action::EnterM13Receive: enter_m13_receive(); break;
            case Action::ExitM13Receive:
                m13_manual_exit_requested_ = true;
                dirty_ = true;
                break;
            case Action::ExitM2:
                image_.clear();
                catalog_.clear();
                state_.exit_m2();
                dirty_ = true;
                break;
            case Action::ExitM3:
            case Action::ExitM13:
                exit_bluetooth_mode();
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
                if (!confirmation_pending_) {
                    confirmation_pending_ = true;
                    confirmation_generation_ = snapshot.generation;
                    m1_->request_confirmation(snapshot.generation);
                }
                break;
            case Action::OpenFile: {
                char filename[Storage::GBR_NAME_CAPACITY]{};
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
                if (state_.mode() == Mode::M3) {
                    if (prepare_bt_layout()) {
                        start_m3_print();
                    } else {
                        state_.label_warning();
                    }
                    dirty_ = true;
                } else {
                    ensure_worker_started();
                    if (worker.mode() == Gbp::Terminal::Worker::Mode::Idle) {
                        if (select_first_print_block()) {
                            printed_bytes_ = 0;
                            start_m2_block();
                            state_.print_started();
                            drawn_print_progress_ = 0;
                        }
                        dirty_ = true;
                    }
                }
                break;
            case Action::ContinueLabelPrint:
                start_m3_print();
                dirty_ = true;
                break;
            case Action::CancelLabelPrint:
                state_.label_warning_cancelled();
                dirty_ = true;
                break;
            case Action::CancelM3Print:
                bt_printer_.disconnect();
                image_.clear();
                state_.m3_connection_lost();
                dirty_ = true;
                break;
        }
    }

    bool draw_m1(const Gbp::Terminal::M1State::Snapshot& snapshot) {
        switch (snapshot.view) {
            case Gbp::Terminal::ViewState::Waiting: return Gbp::Terminal::draw_waiting();
            case Gbp::Terminal::ViewState::Receiving: return Gbp::Terminal::draw_receiving(snapshot.frames);
            case Gbp::Terminal::ViewState::Preview:
                if (confirmation_pending_) {
                    return false;
                }
                if (frame_owned_ && active_frame_.generation == snapshot.generation) {
                    Gbp::Terminal::draw_preview(active_frame_.data, active_frame_.size, active_frame_.palette);
                    return true;
                }
                return drawn_mode_ == Mode::M1 && drawn_m1_.view == Gbp::Terminal::ViewState::Preview &&
                       drawn_m1_.generation == snapshot.generation;
        }
        return false;
    }

    bool draw_screen(const Gbp::Terminal::M1State::Snapshot& snapshot) {
        switch (state_.mode()) {
            case Mode::Menu:
                return state_.menu_page() == MenuPage::Error ? Screen::draw_error(menu_error_) :
                    Screen::draw_menu(state_.menu_selection());
            case Mode::Settings:
                return Screen::draw_settings(state_.setting_selection(), state_.setting_editing(),
                                             settings_error_);
            case Mode::M1: return draw_m1(snapshot);
            case Mode::M2:
                switch (state_.m2_page()) {
                    case M2Page::List: return Screen::draw_file_list(catalog_, state_.selected_file());
                    case M2Page::Preview:
                        Gbp::Terminal::draw_preview(image_.data(), image_.size(),
                                                    Gbr::Format::preview_palette(image_.palette()));
                        return Screen::draw_block_indicator(image_.block_index(), image_.block_count());
                    case M2Page::Printing:
                        return Screen::draw_printing(printed_bytes_ + worker.print_progress(), image_.total_size());
                    case M2Page::LabelWarning: return false;
                    case M2Page::Error: return Screen::draw_error(error_text_);
                }
            case Mode::M3:
            case Mode::M13:
                switch (state_.m3_page()) {
                    case M3Page::Menu:
                        return Screen::draw_m3_menu(state_.m3_selection(), bt_printer_.connected(),
                                                    state_.mode() == Mode::M13);
                    case M3Page::Pairing: {
                        std::string_view status = "Disconnected";
                        switch (bt_printer_.connection_state()) {
                            case BtPrinter::ConnectionState::Disconnected: break;
                            case BtPrinter::ConnectionState::Connecting: status = "Connecting"; break;
                            case BtPrinter::ConnectionState::Connected: status = "Connected"; break;
                            case BtPrinter::ConnectionState::Error: status = "Pair failed"; break;
                        }
                        return Screen::draw_m3_pairing(state_.m3_pair_selection(), status);
                    }
                    case M3Page::Files:
                        switch (state_.m2_page()) {
                            case M2Page::List: return Screen::draw_file_list(catalog_, state_.selected_file(), true);
                            case M2Page::Preview:
                                Gbp::Terminal::draw_preview(image_.data(), image_.size(),
                                                            Gbr::Format::preview_palette(image_.palette()));
                                return Screen::draw_block_indicator(image_.block_index(), image_.block_count());
                            case M2Page::Printing:
                                return Screen::draw_printing(bt_printer_.progress(), bt_printer_.total(), true);
                            case M2Page::LabelWarning:
                                return Screen::draw_label_warning(label_layout_.content_dots,
                                                                  label_layout_.label_dots, true);
                            case M2Page::Error: return Screen::draw_error(error_text_);
                        }
                    case M3Page::Receive:
                        if (!m13_printing_ && !m13_cut_pending_ &&
                            snapshot.view == Gbp::Terminal::ViewState::Receiving) {
                            return Gbp::Terminal::draw_receiving(snapshot.frames);
                        }
                        return Screen::draw_m13_receive(snapshot.cut_pending, snapshot.cut_remaining_us,
                                                        frame_owned_ || m13_group_saved_ ||
                                                            snapshot.view == Gbp::Terminal::ViewState::Preview,
                                                        m13_cut_pending_, m13_printing_, m13_error_);
                }
        }
        return false;
    }

    void render() {
        if (!dirty_ || Display::is_presenting()) {
            return;
        }
#if defined(KP_BT_DEBUG)
        const bool entering_m3_display = drawn_mode_ != Mode::M3 && state_.mode() == Mode::M3;
#endif
        const auto snapshot = m1_ ? m1_->snapshot(time_us_32()) : Gbp::Terminal::M1State::Snapshot{};
        if (!draw_screen(snapshot)) {
            return;
        }
        if ((state_.mode() == Mode::M3 || state_.mode() == Mode::M13) && bt_printer_.connected() &&
            !Screen::draw_paired_badge()) {
            return;
        }
        if (!Display::present()) {
            return;
        }
        drawn_mode_ = state_.mode();
        if (drawn_mode_ == Mode::M1) {
            drawn_m1_ = snapshot;
            if (snapshot.view == Gbp::Terminal::ViewState::Preview && frame_owned_ &&
                active_frame_.generation == snapshot.generation) {
                frame_displayed_or_skipped_ = true;
            }
        }
        if (drawn_mode_ == Mode::M2 && state_.m2_page() == M2Page::Printing) {
            drawn_print_progress_ = worker.print_progress();
        }
        if (drawn_mode_ == Mode::M3 && state_.m3_page() == M3Page::Files && state_.m2_page() == M2Page::Printing) {
            drawn_print_progress_ = bt_printer_.progress();
        }
        if (drawn_mode_ == Mode::M13 && state_.m3_page() == M3Page::Receive) {
            drawn_m13_ = snapshot;
            drawn_m13_tenths_ = snapshot.cut_pending ?
                (snapshot.cut_remaining_us + 99'999u) / 100'000u : UINT32_MAX;
        }
        dirty_ = false;
#if defined(KP_BT_DEBUG)
        if (entering_m3_display) {
            printf("M3 screen submitted page=%u\n", static_cast<unsigned>(state_.m2_page()));
        }
#endif
    }

    void start_m2_block() {
        const auto control = Gbr::Format::m2_print_control(image_.print_control());
        printf("M2 block %lu/%lu: recorded sheets=%u, sending sheets=1\n",
               static_cast<unsigned long>(image_.block_index() + 1),
               static_cast<unsigned long>(image_.block_count()),
               static_cast<unsigned>(image_.print_control()[0]));
        worker.start_print(image_.data(), image_.size(), control, Config::Vars::rx_sample_delay_us.get());
    }

    bool card_mounted_ = false;
    bool storage_ready_ = false;
    bool worker_started_ = false;
    BtPrinter::BluetoothPrinter bt_printer_{};
    BtPrinter::ConnectionState drawn_connection_state_ = BtPrinter::ConnectionState::Disconnected;
    StateMachine state_{};
    KeyScanner keys_{};
    Storage::GbrStore store_{};
    Storage::GbrCatalog catalog_{};
    Storage::GbrImage image_{};
    BtPrinter::LabelLayout label_layout_{};
    std::unique_ptr<Gbp::Terminal::M1State> m1_{};
    Gbp::Terminal::M1State::Frame active_frame_{};
    Gbp::Terminal::M1State::Snapshot drawn_m1_{};
    Gbp::Terminal::M1State::Snapshot drawn_m13_{};
    Mode drawn_mode_ = Mode::Menu;
    std::string_view menu_error_ = "SD CARD ERROR";
    std::string_view error_text_ = "GBR error";
    usize drawn_print_progress_ = 0;
    usize printed_bytes_ = 0;
    uint32 confirmation_generation_ = 0;
    bool frame_owned_ = false;
    bool frame_displayed_or_skipped_ = false;
    bool save_active_ = false;
    bool leaving_m1_ = false;
    bool confirmation_pending_ = false;
    uint32 m13_cut_seen_ = 0;
    uint32 m13_discarded_seen_ = 0;
    uint32 drawn_m13_tenths_ = UINT32_MAX;
    std::string_view m13_error_{};
    bool m13_cut_pending_ = false;
    bool m13_group_saved_ = false;
    bool m13_save_failed_ = false;
    bool m13_printing_ = false;
    bool m13_stop_requested_ = false;
    bool m13_manual_exit_requested_ = false;
    uint32 setting_original_ = 0;
    std::string_view settings_error_{};
    bool dirty_ = true;
};

}  // namespace

void run(const bool card_mounted) {
    // Keep application state out of Core 0's limited call stack.
    static Application application(card_mounted);
    application.run();
}

}  // namespace App
