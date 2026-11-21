// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <cassert>

#include "app/state_machine.h"

int main() {
    using namespace App;
    StateMachine state;
    assert(state.mode() == Mode::Menu);
    assert(state.on_key(Key::Left, false, false, false) == Action::None);
    state.menu_failed();
    assert(state.menu_page() == MenuPage::Error);
    assert(state.on_key(Key::Down, false, false, false) == Action::None);
    assert(state.on_key(Key::Left, false, false, false) == Action::Redraw);
    assert(state.menu_page() == MenuPage::Select);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::EnterM1);
    state.enter_m1();
    assert(state.on_key(Key::Left, false, false, false) == Action::None);
    assert(state.on_key(Key::Left, true, false, false) == Action::ExitM1);
    state.exit_m1();
    assert(state.on_key(Key::Down, false, false, false) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::EnterM2);
    state.enter_m2(3);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::OpenFile);
    assert(state.on_key(Key::Down, false, false, false) == Action::Redraw);
    assert(state.selected_file() == 1);
    state.file_opened(3);
    assert(state.selected_block() == 0);
    assert(state.on_key(Key::Down, false, false, false) == Action::NextBlock);
    assert(state.selected_block() == 1);
    assert(state.on_key(Key::Down, false, false, false) == Action::NextBlock);
    assert(state.selected_block() == 2);
    assert(state.on_key(Key::Down, false, false, false) == Action::None);
    assert(state.on_key(Key::Up, false, false, false) == Action::PreviousBlock);
    assert(state.selected_block() == 1);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::StartPrint);
    state.print_started();
    assert(state.on_key(Key::Left, false, false, false) == Action::None);
    state.print_finished(true);
    assert(state.m2_page() == M2Page::List && state.selected_file() == 1);
    assert(state.on_key(Key::Left, false, false, false) == Action::ExitM2);
    state.exit_m2();
    assert(state.mode() == Mode::Menu);
    assert(state.on_key(Key::Down, false, false, false) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::EnterM3);
    state.enter_m3();
    assert(state.m3_page() == M3Page::Menu);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::Redraw);
    assert(state.m3_page() == M3Page::Pairing);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::PairM3);
    assert(state.on_key(Key::Down, false, false, false) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::DisconnectM3);
    assert(state.on_key(Key::Left, false, false, false) == Action::Redraw);
    assert(state.m3_page() == M3Page::Menu);
    assert(state.on_key(Key::Down, false, false, false) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::Redraw);
    assert(state.m3_page() == M3Page::Pairing);  // Print requires a live link.
    assert(state.m3_pair_selection() == 0);
    assert(state.on_key(Key::Left, false, false, false) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::EnterM3Files);
    assert(state.m3_page() == M3Page::Files);
    state.m3_files_ready(1);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::OpenFile);
    state.file_opened(1);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::StartPrint);
    state.print_started();
    assert(state.on_key(Key::Left, false, false, true) == Action::CancelM3Print);
    state.m3_connection_lost();
    assert(state.m3_page() == M3Page::Pairing);
    assert(state.m3_pair_selection() == 0);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::PairM3);
    assert(state.on_key(Key::Left, false, false, false) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::EnterM3Files);
    state.m3_files_ready(2);
    assert(state.file_count() == 2);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::OpenFile);
    state.file_opened(1);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::StartPrint);
    state.print_started();
    state.print_finished(true);
    assert(state.m2_page() == M2Page::List);
    assert(state.on_key(Key::Left, false, false, true) == Action::Redraw);
    assert(state.m3_page() == M3Page::Menu);
    assert(state.on_key(Key::Left, false, false, false) == Action::ExitM3);
    state.exit_m3();
    assert(state.mode() == Mode::Menu);
    assert(state.on_key(Key::Down, false, false, false) == Action::Redraw);
    assert(state.menu_selection() == 3);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::EnterM13);
    state.enter_m13();
    assert(state.mode() == Mode::M13 && state.m3_page() == M3Page::Menu);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::Redraw);
    assert(state.m3_page() == M3Page::Pairing);
    assert(state.on_key(Key::Confirm, false, false, false) == Action::PairM3);
    assert(state.on_key(Key::Left, false, false, true) == Action::Redraw);
    assert(state.on_key(Key::Down, false, false, true) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::EnterM13Receive);
    state.m13_receive_started();
    assert(state.m3_page() == M3Page::Receive);
    assert(state.on_key(Key::Left, true, false, true) == Action::ExitM13Receive);
    state.m13_receive_finished();
    assert(state.m3_page() == M3Page::Menu);
    assert(state.on_key(Key::Confirm, false, false, true) == Action::EnterM13Receive);
    state.m13_receive_started();
    state.m3_connection_lost();
    assert(state.m3_page() == M3Page::Pairing);
    assert(state.on_key(Key::Left, false, false, false) == Action::Redraw);
    assert(state.on_key(Key::Left, false, false, false) == Action::ExitM13);
    state.exit_m13();
    assert(state.mode() == Mode::Menu);

    StateMachine settings;
    for (int index = 0; index < 4; ++index) {
        assert(settings.on_key(Key::Down, false, false, false) == Action::Redraw);
    }
    assert(settings.on_key(Key::Confirm, false, false, false) == Action::EnterSettings);
    settings.enter_settings();
    assert(settings.mode() == Mode::Settings);
    assert(settings.on_key(Key::Confirm, false, false, false) == Action::BeginSetting);
    assert(settings.setting_editing());
    assert(settings.on_key(Key::Up, false, false, false) == Action::IncreaseSetting);
    assert(settings.on_key(Key::Right, false, false, false) == Action::None);
    assert(settings.on_key(Key::Left, false, false, false) == Action::CancelSetting);
    assert(!settings.setting_editing());
    assert(settings.on_key(Key::Down, false, false, false) == Action::Redraw);
    assert(settings.setting_selection() == Setting::Palette);
    assert(settings.on_key(Key::Confirm, false, false, false) == Action::BeginSetting);
    assert(settings.on_key(Key::Confirm, false, false, false) == Action::SaveSetting);
    settings.setting_saved();
    assert(!settings.setting_editing());
    assert(settings.on_key(Key::Left, false, false, false) == Action::ExitSettings);
    settings.exit_settings();
    assert(settings.mode() == Mode::Menu);
}
