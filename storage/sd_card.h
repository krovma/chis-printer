// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <string_view>

#include "types.h"

namespace Storage {

// Leave room for all per-mode left margins in an existing 4 KiB config.
inline constexpr usize MAX_CONFIG_BYTES = 5 * 1024;

// Requires the active-low card-detect GPIO to have been initialized.
bool mount_initialized_card();
bool read_config_text(std::string_view& output);
bool write_config_value(std::string_view path, std::string_view toml_value);

}  // namespace Storage
