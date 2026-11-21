// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <string_view>

#include "types.h"

namespace Config {

bool update_toml_value(std::string_view input, std::string_view path, std::string_view value,
                       char* output, usize capacity, usize& output_size);

}  // namespace Config
