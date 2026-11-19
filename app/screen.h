// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "storage/gbr_library.h"

namespace app::screen {

bool draw_menu(std::uint8_t selection);
bool draw_file_list(const storage::GbrCatalog& catalog, std::size_t selection);
bool draw_printing(std::size_t sent, std::size_t total);
bool draw_block_indicator(std::size_t index, std::size_t count);
bool draw_error(std::string_view message);

}  // namespace app::screen
