// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstddef>
#include <cstdint>

namespace gbp::terminal {

bool draw_waiting();
bool draw_receiving(std::uint32_t data_frames);
void draw_preview(const std::uint8_t* data, std::size_t size, std::uint8_t palette);

}  // namespace gbp::terminal
