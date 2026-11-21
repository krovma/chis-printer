// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <string_view>

namespace Storage {

// Requires the active-low card-detect GPIO to have been initialized.
bool mount_initialized_card();
bool read_config_text(std::string_view& output);
bool write_config_value(std::string_view path, std::string_view toml_value);

}  // namespace Storage
