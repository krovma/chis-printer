// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/state_machine.h"
#include "types.h"

namespace App {

Action StateMachine::on_key(const Key key, const bool m1_waiting, const bool m1_preview,
                            const bool m3_connected) {
    switch (mode_) {
        case Mode::Menu:
            if (menu_page_ == MenuPage::Error) {
                if (key == Key::Left || key == Key::Confirm) {
                    menu_page_ = MenuPage::Select;
                    return Action::Redraw;
                }
                return Action::None;
            }
            if (key == Key::Up && menu_selection_ > 0) {
                --menu_selection_;
                return Action::Redraw;
            }
            if (key == Key::Down && menu_selection_ < 4) {
                ++menu_selection_;
                return Action::Redraw;
            }
            if (key == Key::Confirm) {
                if (menu_selection_ == 0) {
                    return Action::EnterM1;
                }
                if (menu_selection_ == 1) {
                    return Action::EnterM2;
                }
                if (menu_selection_ == 2) {
                    return Action::EnterM3;
                }
                if (menu_selection_ == 3) {
                    return Action::EnterM13;
                }
                return Action::EnterSettings;
            }
            return Action::None;
        case Mode::Settings:
            if (setting_editing_) {
                if (key == Key::Left) {
                    setting_editing_ = false;
                    return Action::CancelSetting;
                }
                if (key == Key::Up) return Action::IncreaseSetting;
                if (key == Key::Down) return Action::DecreaseSetting;
                if (key == Key::Confirm) return Action::SaveSetting;
                return Action::None;
            }
            if (key == Key::Left) return Action::ExitSettings;
            if (key == Key::Up && setting_selection_ > 0) {
                --setting_selection_;
                return Action::Redraw;
            }
            if (key == Key::Down && setting_selection_ + 1 < static_cast<uint8>(Setting::Count)) {
                ++setting_selection_;
                return Action::Redraw;
            }
            if (key == Key::Confirm) {
                setting_editing_ = true;
                return Action::BeginSetting;
            }
            return Action::None;
        case Mode::M1:
            if (key == Key::Left && m1_waiting) {
                return Action::ExitM1;
            }
            if (key == Key::Confirm && m1_preview) {
                return Action::ConfirmM1Preview;
            }
            if (key == Key::Up) {
                return Action::PaletteDown;
            }
            if (key == Key::Down) {
                return Action::PaletteUp;
            }
            return Action::None;
        case Mode::M2:
        case Mode::M3:
        case Mode::M13:
            if (mode_ == Mode::M3 || mode_ == Mode::M13) {
                if (m3_page_ == M3Page::Menu) {
                    if (key == Key::Left) {
                        return mode_ == Mode::M3 ? Action::ExitM3 : Action::ExitM13;
                    }
                    if (key == Key::Up && m3_selection_ > 0) {
                        --m3_selection_;
                        return Action::Redraw;
                    }
                    if (key == Key::Down && m3_selection_ < 1) {
                        ++m3_selection_;
                        return Action::Redraw;
                    }
                    if (key == Key::Confirm) {
                        if (m3_selection_ == 0 || !m3_connected) {
                            m3_page_ = M3Page::Pairing;
                            m3_pair_selection_ = 0;
                        } else {
                            if (mode_ == Mode::M13) {
                                return Action::EnterM13Receive;
                            }
                            m3_page_ = M3Page::Files;
                            m2_page_ = M2Page::List;
                            return Action::EnterM3Files;
                        }
                        return Action::Redraw;
                    }
                    return Action::None;
                }
                if (m3_page_ == M3Page::Pairing) {
                    if (key == Key::Left) {
                        m3_page_ = M3Page::Menu;
                        return Action::Redraw;
                    }
                    if (key == Key::Up && m3_pair_selection_ > 0) {
                        --m3_pair_selection_;
                        return Action::Redraw;
                    }
                    if (key == Key::Down && m3_pair_selection_ < 1) {
                        ++m3_pair_selection_;
                        return Action::Redraw;
                    }
                    if (key == Key::Confirm) {
                        return m3_pair_selection_ == 0 ? Action::PairM3 : Action::DisconnectM3;
                    }
                    return Action::None;
                }
                if (m3_page_ == M3Page::Receive) {
                    return key == Key::Left ? Action::ExitM13Receive : Action::None;
                }
                if (!m3_connected) {
                    return Action::None;
                }
            }
            switch (m2_page_) {
                case M2Page::List:
                    if (key == Key::Left) {
                        if (mode_ == Mode::M2) {
                            return Action::ExitM2;
                        }
                        m3_page_ = M3Page::Menu;
                        return Action::Redraw;
                    }
                    if (key == Key::Up && selected_file_ > 0) {
                        --selected_file_;
                        return Action::Redraw;
                    }
                    if (key == Key::Down && selected_file_ + 1 < file_count_) {
                        ++selected_file_;
                        return Action::Redraw;
                    }
                    if (key == Key::Confirm && file_count_ != 0) {
                        return Action::OpenFile;
                    }
                    return Action::None;
                case M2Page::Preview:
                    if (key == Key::Left) {
                        return Action::CloseFile;
                    }
                    if (key == Key::Up && selected_block_ > 0) {
                        --selected_block_;
                        return Action::PreviousBlock;
                    }
                    if (key == Key::Down && selected_block_ + 1 < block_count_) {
                        ++selected_block_;
                        return Action::NextBlock;
                    }
                    if (key == Key::Confirm) {
                        return Action::StartPrint;
                    }
                    return Action::None;
                case M2Page::Printing:
                    return mode_ == Mode::M3 && key == Key::Left ? Action::CancelM3Print : Action::None;
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
void StateMachine::enter_m2(const usize file_count) {
    mode_ = Mode::M2;
    m2_page_ = M2Page::List;
    file_count_ = file_count;
    selected_file_ = 0;
}
void StateMachine::exit_m2() {
    mode_ = Mode::Menu;
    menu_page_ = MenuPage::Select;
}
void StateMachine::enter_m3() {
    mode_ = Mode::M3;
    m3_page_ = M3Page::Menu;
    m2_page_ = M2Page::List;
    m3_selection_ = 0;
    m3_pair_selection_ = 0;
    file_count_ = 0;
    selected_file_ = 0;
}
void StateMachine::exit_m3() {
    mode_ = Mode::Menu;
    menu_page_ = MenuPage::Select;
}
void StateMachine::enter_m13() {
    enter_m3();
    mode_ = Mode::M13;
}
void StateMachine::exit_m13() { exit_m3(); }
void StateMachine::enter_settings() {
    mode_ = Mode::Settings;
    setting_selection_ = 0;
    setting_editing_ = false;
}
void StateMachine::exit_settings() {
    mode_ = Mode::Menu;
    setting_editing_ = false;
}
void StateMachine::setting_saved() { setting_editing_ = false; }
void StateMachine::m13_receive_started() {
    if (mode_ == Mode::M13 && m3_page_ == M3Page::Menu) {
        m3_page_ = M3Page::Receive;
    }
}
void StateMachine::m13_receive_finished() {
    if (mode_ == Mode::M13 && m3_page_ == M3Page::Receive) {
        m3_page_ = M3Page::Menu;
    }
}
void StateMachine::m3_connection_lost() {
    if ((mode_ != Mode::M3 || m3_page_ != M3Page::Files) && (mode_ != Mode::M13 || m3_page_ != M3Page::Receive)) {
        return;
    }
    m3_page_ = M3Page::Pairing;
    m3_pair_selection_ = 0;
    m2_page_ = M2Page::List;
}
void StateMachine::m3_files_ready(const usize file_count) {
    if (mode_ != Mode::M3 || m3_page_ != M3Page::Files) {
        return;
    }
    file_count_ = file_count;
    selected_file_ = 0;
}
void StateMachine::file_opened(const usize block_count) {
    block_count_ = block_count;
    selected_block_ = 0;
    m2_page_ = M2Page::Preview;
}
void StateMachine::file_failed() { m2_page_ = M2Page::Error; }
void StateMachine::file_closed() { m2_page_ = M2Page::List; }
void StateMachine::print_started() { m2_page_ = M2Page::Printing; }
void StateMachine::print_finished(const bool success) { m2_page_ = success ? M2Page::List : M2Page::Error; }

}  // namespace App
