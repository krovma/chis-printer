// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/state_machine.h"

namespace app {

Action StateMachine::on_key(const Key key, const bool m1_waiting, const bool m1_preview) {
    switch (mode_) {
        case Mode::Menu:
            if (menu_page_ == MenuPage::Error) {
                if (key == Key::Left || key == Key::Confirm) {
                    menu_page_ = MenuPage::Select;
                    return Action::Redraw;
                }
                return Action::None;
            }
            if (key == Key::Up && menu_selection_ != 0) {
                menu_selection_ = 0;
                return Action::Redraw;
            }
            if (key == Key::Down && menu_selection_ != 1) {
                menu_selection_ = 1;
                return Action::Redraw;
            }
            if (key == Key::Left) return Action::BrightnessDown;
            if (key == Key::Right) return Action::BrightnessUp;
            if (key == Key::Confirm) return menu_selection_ == 0 ? Action::EnterM1 : Action::EnterM2;
            return Action::None;
        case Mode::M1:
            if (key == Key::Left && m1_waiting) return Action::ExitM1;
            if (key == Key::Confirm && m1_preview) return Action::ConfirmM1Preview;
            if (key == Key::Up) return Action::PaletteDown;
            if (key == Key::Down) return Action::PaletteUp;
            return Action::None;
        case Mode::M2:
            switch (m2_page_) {
                case M2Page::List:
                    if (key == Key::Left) return Action::ExitM2;
                    if (key == Key::Up && selected_file_ > 0) {
                        --selected_file_;
                        return Action::Redraw;
                    }
                    if (key == Key::Down && selected_file_ + 1 < file_count_) {
                        ++selected_file_;
                        return Action::Redraw;
                    }
                    if (key == Key::Confirm && file_count_ != 0) return Action::OpenFile;
                    return Action::None;
                case M2Page::Preview:
                    if (key == Key::Left) return Action::CloseFile;
                    if (key == Key::Up && selected_block_ > 0) {
                        --selected_block_;
                        return Action::PreviousBlock;
                    }
                    if (key == Key::Down && selected_block_ + 1 < block_count_) {
                        ++selected_block_;
                        return Action::NextBlock;
                    }
                    if (key == Key::Confirm) return Action::StartPrint;
                    return Action::None;
                case M2Page::Printing:
                    return Action::None;
                case M2Page::Error:
                    return key == Key::Left || key == Key::Confirm ? Action::CloseFile : Action::None;
            }
    }
    return Action::None;
}

void StateMachine::menu_failed() { menu_page_ = MenuPage::Error; }
void StateMachine::enter_m1() { mode_ = Mode::M1; }
void StateMachine::exit_m1() {
    mode_ = Mode::Menu;
    menu_page_ = MenuPage::Select;
}
void StateMachine::enter_m2(const std::size_t file_count) {
    mode_ = Mode::M2;
    m2_page_ = M2Page::List;
    file_count_ = file_count;
    selected_file_ = 0;
}
void StateMachine::exit_m2() {
    mode_ = Mode::Menu;
    menu_page_ = MenuPage::Select;
}
void StateMachine::file_opened(const std::size_t block_count) {
    block_count_ = block_count;
    selected_block_ = 0;
    m2_page_ = M2Page::Preview;
}
void StateMachine::file_failed() { m2_page_ = M2Page::Error; }
void StateMachine::file_closed() { m2_page_ = M2Page::List; }
void StateMachine::print_started() { m2_page_ = M2Page::Printing; }
void StateMachine::print_finished(const bool success) { m2_page_ = success ? M2Page::List : M2Page::Error; }

}  // namespace app
