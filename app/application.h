// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "config/firmware_config.h"

namespace app {

// Core0 owns the UI and SD card. Core1 runs the link or printer transport.
void run(bool card_mounted, const config::FirmwareConfig& firmware_config);

}  // namespace app
