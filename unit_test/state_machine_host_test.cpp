// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <cassert>

#include "app/state_machine.h"

int main() {
    using namespace app;
    StateMachine state;
    assert(state.mode() == Mode::Menu);
    assert(state.on_key(Key::Left, false, false) == Action::BrightnessDown);
    state.menu_failed();
    assert(state.menu_page() == MenuPage::Error);
    assert(state.on_key(Key::Down, false, false) == Action::None);
    assert(state.on_key(Key::Left, false, false) == Action::Redraw);
    assert(state.menu_page() == MenuPage::Select);
    assert(state.on_key(Key::Confirm, false, false) == Action::EnterM1);
    state.enter_m1();
    assert(state.on_key(Key::Left, false, false) == Action::None);
    assert(state.on_key(Key::Left, true, false) == Action::ExitM1);
    state.exit_m1();
    assert(state.on_key(Key::Down, false, false) == Action::Redraw);
    assert(state.on_key(Key::Confirm, false, false) == Action::EnterM2);
    state.enter_m2(3);
    assert(state.on_key(Key::Confirm, false, false) == Action::OpenFile);
    assert(state.on_key(Key::Down, false, false) == Action::Redraw);
    assert(state.selected_file() == 1);
    state.file_opened(3);
    assert(state.selected_block() == 0);
    assert(state.on_key(Key::Down, false, false) == Action::NextBlock);
    assert(state.selected_block() == 1);
    assert(state.on_key(Key::Down, false, false) == Action::NextBlock);
    assert(state.selected_block() == 2);
    assert(state.on_key(Key::Down, false, false) == Action::None);
    assert(state.on_key(Key::Up, false, false) == Action::PreviousBlock);
    assert(state.selected_block() == 1);
    assert(state.on_key(Key::Confirm, false, false) == Action::StartPrint);
    state.print_started();
    assert(state.on_key(Key::Left, false, false) == Action::None);
    state.print_finished(true);
    assert(state.m2_page() == M2Page::List && state.selected_file() == 1);
    assert(state.on_key(Key::Left, false, false) == Action::ExitM2);
    state.exit_m2();
    assert(state.mode() == Mode::Menu);
}
