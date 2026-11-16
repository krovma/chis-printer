// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "gbp/terminal/receiver.h"

namespace gbp::terminal {

bool draw_waiting();
bool draw_receiving(std::uint32_t data_frames);
void draw_preview(const Receiver& receiver);

}  // namespace gbp::terminal
