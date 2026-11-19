// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

enum class Key : std::uint8_t { None, Up, Down, Left, Right, Confirm };
enum class Mode : std::uint8_t { Menu, M1, M2, M3 };
enum class MenuPage : std::uint8_t { Select, Error };
enum class M2Page : std::uint8_t { List, Preview, Printing, Error };
enum class Action : std::uint8_t {
    None, Redraw, BrightnessDown, BrightnessUp, PaletteDown, PaletteUp,
    EnterM1, ExitM1, EnterM2, ExitM2, EnterM3, ExitM3, ConfirmM1Preview,
    OpenFile, CloseFile, PreviousBlock, NextBlock, StartPrint, CancelM3Print,
};

class StateMachine {
  public:
    Mode mode() const { return mode_; }
    MenuPage menu_page() const { return menu_page_; }
    M2Page m2_page() const { return m2_page_; }
    std::uint8_t menu_selection() const { return menu_selection_; }
    std::size_t file_count() const { return file_count_; }
    std::size_t selected_file() const { return selected_file_; }
    std::size_t selected_block() const { return selected_block_; }

    Action on_key(Key key, bool m1_waiting, bool m1_preview);
    void menu_failed();
    void enter_m1();
    void exit_m1();
    void enter_m2(std::size_t file_count);
    void exit_m2();
    void enter_m3(std::size_t file_count);
    void exit_m3();
    void file_opened(std::size_t block_count);
    void file_failed();
    void file_closed();
    void print_started();
    void print_finished(bool success);

  private:
    Mode mode_ = Mode::Menu;
    MenuPage menu_page_ = MenuPage::Select;
    M2Page m2_page_ = M2Page::List;
    std::uint8_t menu_selection_ = 0;
    std::size_t file_count_ = 0;
    std::size_t selected_file_ = 0;
    std::size_t block_count_ = 0;
    std::size_t selected_block_ = 0;
};

}  // namespace app
