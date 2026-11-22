// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "config/cvar.h"
#include "config/printer_config.h"
#include "display/ui/text.h"

namespace Config::Vars {

extern CVar<Display::Ui::Locale> locale;
extern CVar<uint8> palette;
extern CVar<uint8> backlight_percent;
extern CVar<uint32> auto_cut_time_limit_us;
extern CVar<uint32> simulated_print_time_us;
extern CVar<uint8> rx_sample_delay_us;
extern CVar<PrinterType> printer_type;
extern CVar<PaperType> paper_type;
extern CVar<PrintProtocol> print_protocol;
extern CVar<CenteringMode> centering;
extern CVar<uint16> label_length_mm;

// Bluetooth connection and paper details stored in TOML.
extern CVar<std::array<char, 32>> printer_name;
extern CVar<std::array<char, 8>> pin;
extern CVar<uint16> label_gap_mm;

M3Config current_m3_config();
bool set_m3_image_scale(uint8 scale_quarters, WriteMode mode);
bool set_m3_left_margin_dots(uint16 dots, WriteMode mode);

}  // namespace Config::Vars
