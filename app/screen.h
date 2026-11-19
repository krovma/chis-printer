// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "storage/gbr_library.h"

namespace app::screen {

bool draw_menu(std::uint8_t selection);
bool draw_m3_menu(std::uint8_t selection, bool connected, bool m13 = false);
bool draw_m3_pairing(std::uint8_t selection, std::string_view status);
bool draw_m13_receive(bool countdown, std::uint32_t remaining_us, bool awaiting_cut, bool saving,
                      bool printing, std::string_view error);
bool draw_paired_badge();
bool draw_file_list(const storage::GbrCatalog& catalog, std::size_t selection, bool bluetooth = false);
bool draw_printing(std::size_t sent, std::size_t total, bool can_cancel = false);
bool draw_block_indicator(std::size_t index, std::size_t count);
bool draw_error(std::string_view message);

}  // namespace app::screen
