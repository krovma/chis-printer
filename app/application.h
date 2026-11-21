// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

namespace App {

// Core0 owns the UI and SD card. Core1 runs the link or printer transport.
void run(bool card_mounted);

}  // namespace App
