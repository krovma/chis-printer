// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"

namespace Gbp::Terminal {

bool draw_waiting();
bool draw_receiving(uint32 data_frames);
void draw_preview(const uint8* data, usize size, uint8 palette);

}  // namespace Gbp::Terminal
