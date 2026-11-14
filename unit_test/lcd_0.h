#pragma once

namespace unit_test::lcd_0 {

// Initialize the LCD and reset the palette test state. The display module
// applies its default 50% backlight preset after the first frame completes.
void init();

// Advance the palette and frame timers. This function is
// non-blocking and should be called continuously from the application loop.
void update();

}  // namespace unit_test::lcd_0
