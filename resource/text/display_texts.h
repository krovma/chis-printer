// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <string_view>

namespace FirmwareText {

struct Translation {
    std::string_view locale;
    std::string_view key;
    std::string_view value;
};

// Every string that may be rendered on the LCD belongs in this table. The
// font generator reads the value column to keep the glyph set synchronized.
inline constexpr std::array<Translation, 25> DISPLAY_TEXTS{{
    {"en", "sd_card_error", u8"SD CARD ERROR"},
    {"en", "m1_waiting", u8"Waiting"},
    {"en", "m1_receiving", u8"Receiving 0123456789"},
    {"en", "menu", u8"Menu"},
    {"en", "mode_m1", u8"M1"},
    {"en", "mode_m2", u8"M2"},
    {"en", "mode_m3", u8"M3"},
    {"en", "mode_m13", u8"M1+3"},
    {"en", "gbr_files", u8"GBR files"},
    {"en", "bt_files", u8"BT files"},
    {"en", "no_gbr", u8"No GBR files"},
    {"en", "printing", u8"Printing"},
    {"en", "cancel_print", u8"Left cancel"},
    {"en", "gbr_error", u8"GBR error"},
    {"en", "print_error", u8"Print error"},
    {"en", "bt_error", u8"BT error"},
    {"en", "bt_print_error", u8"BT print error"},
    {"en", "m1_error", u8"M1 error"},
    {"en", "m13_error", u8"M1+3 error"},
    {"en", "m13_auto_cut", u8"AutoCut"},
    {"en", "m13_saving", u8"Saving GBR"},
    {"en", "m13_printing", u8"BT Printing"},
    {"en", "m13_save_error", u8"GBR save error"},
    {"en", "m13_read_error", u8"GBR read error"},
    {"en", "filename_glyphs", u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-+? >/%"},
}};

}  // namespace FirmwareText
