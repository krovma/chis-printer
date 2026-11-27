// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <string_view>

#include "types.h"
#include "app/state_machine.h"
#include "storage/gbr_library.h"

namespace App::Screen {

bool draw_menu(uint8 selection);
bool draw_settings(Setting selection, bool editing, std::string_view error,
                   bool label_paper = false, bool tspl = false);
bool draw_m3_menu(uint8 selection, bool connected, bool m13 = false);
bool draw_m3_pairing(uint8 selection);
bool draw_m13_receive(bool countdown, uint32 remaining_us, bool awaiting_cut, bool saving,
                      bool printing, std::string_view error);
bool draw_file_list(const Storage::GbrCatalog& catalog, usize selection, bool bluetooth = false);
bool draw_printing(usize sent, usize total, bool can_cancel = false);
bool draw_label_warning(usize content_dots, usize label_dots, bool interactive);
bool draw_block_indicator(usize index, usize count);
bool draw_error(std::string_view message);

}  // namespace App::Screen
