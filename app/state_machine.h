// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"

namespace App {

enum class Key : uint8 { None, Up, Down, Left, Right, Confirm };
enum class Mode : uint8 { Menu, M1, M2, M3, M13, Settings };
enum class MenuPage : uint8 { Select, Error };
enum class M2Page : uint8 { List, Preview, Printing, Error };
enum class M3Page : uint8 { Menu, Pairing, Files, Receive };
enum class Setting : uint8 { Locale, Palette, Backlight, AutoCut, PrintTime, RxDelay, PrinterType, Count };
enum class Action : uint8 {
    None, Redraw, PaletteDown, PaletteUp,
    EnterM1, ExitM1, EnterM2, ExitM2, EnterM3, ExitM3, ConfirmM1Preview,
    OpenFile, CloseFile, PreviousBlock, NextBlock, StartPrint, CancelM3Print,
    PairM3, DisconnectM3, EnterM3Files, EnterM13, EnterM13Receive, ExitM13,
    ExitM13Receive, EnterSettings, ExitSettings, BeginSetting, CancelSetting,
    DecreaseSetting, IncreaseSetting, SaveSetting,
};

// Converts key and lifecycle events into actions for the application to execute.
class StateMachine {
  public:
    Mode mode() const { return mode_; }
    MenuPage menu_page() const { return menu_page_; }
    M2Page m2_page() const { return m2_page_; }
    M3Page m3_page() const { return m3_page_; }
    uint8 m3_selection() const { return m3_selection_; }
    uint8 m3_pair_selection() const { return m3_pair_selection_; }
    uint8 menu_selection() const { return menu_selection_; }
    Setting setting_selection() const { return static_cast<Setting>(setting_selection_); }
    bool setting_editing() const { return setting_editing_; }
    usize file_count() const { return file_count_; }
    usize selected_file() const { return selected_file_; }
    usize selected_block() const { return selected_block_; }

    Action on_key(Key key, bool m1_waiting, bool m1_preview, bool m3_connected);
    void menu_failed();
    void enter_m1();
    void exit_m1();
    void enter_m2(usize file_count);
    void exit_m2();
    void enter_m3();
    void exit_m3();
    void enter_m13();
    void exit_m13();
    void enter_settings();
    void exit_settings();
    void setting_saved();
    void m13_receive_started();
    void m13_receive_finished();
    void m3_connection_lost();
    void m3_files_ready(usize file_count);
    void file_opened(usize block_count);
    void file_failed();
    void file_closed();
    void print_started();
    void print_finished(bool success);

  private:
    Mode mode_ = Mode::Menu;
    MenuPage menu_page_ = MenuPage::Select;
    M2Page m2_page_ = M2Page::List;
    M3Page m3_page_ = M3Page::Menu;
    uint8 menu_selection_ = 0;
    uint8 setting_selection_ = 0;
    bool setting_editing_ = false;
    uint8 m3_selection_ = 0;
    uint8 m3_pair_selection_ = 0;
    usize file_count_ = 0;
    usize selected_file_ = 0;
    usize block_count_ = 0;
    usize selected_block_ = 0;
};

}  // namespace App
