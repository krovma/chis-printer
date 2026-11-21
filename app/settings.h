// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "app/state_machine.h"
#include "config/cvar.h"

namespace App {

uint32 setting_value(Setting setting);
uint32 stepped_setting_value(Setting setting, uint32 current, int direction);
bool set_setting_value(Setting setting, uint32 value, Config::WriteMode mode);

}  // namespace App
