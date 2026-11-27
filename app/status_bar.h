// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"

namespace BtPrinter {
enum class ConnectionState : uint8;
}

namespace Power {
struct BatteryStatus;
}

namespace App::StatusBar {

// Update the status region of the frame buffer before presenting it.
bool draw(BtPrinter::ConnectionState connection, const Power::BatteryStatus& battery);

}  // namespace App::StatusBar
