#pragma once

namespace unit_test::lcd_0 {

// Initialize the LCD and reset the color/backlight test state.
void init();

// Advance the independent backlight and color timers. This function is
// non-blocking and should be called continuously from the application loop.
void update();

}  // namespace unit_test::lcd_0
