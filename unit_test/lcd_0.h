// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

namespace UnitTest::Lcd0 {

// Initialize the LCD and reset the palette test state. The display module
// applies its default 50% backlight preset after the first frame completes.
void init();

// Advance the palette and frame timers. This function is
// non-blocking and should be called continuously from the application loop.
void update();

}  // namespace UnitTest::Lcd0
