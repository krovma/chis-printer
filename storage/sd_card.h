// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "config/display_config.h"

namespace storage {

// Requires the active-low card-detect GPIO to have been initialized.
bool mount_initialized_card();
bool load_display_config(config::DisplayConfig& output);

}  // namespace storage
