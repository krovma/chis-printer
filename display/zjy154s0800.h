#pragma once

#include <cstddef>
#include <cstdint>

namespace display {

inline constexpr uint16_t WIDTH             = 240;
inline constexpr uint16_t HEIGHT            = 240;
inline constexpr size_t   FRAME_BUFFER_SIZE = static_cast<size_t>(WIDTH) * HEIGHT / 2;

// Four-bit grayscale level. The panel receives the corresponding RGB444 color
// 0x000, 0x111, ... 0xFFF when the frame buffer is presented.
using Gray4 = uint8_t;

// Initialize the ZJY154S0800TG02 module with the vendor-provided ST7789VW
// register sequence. The backlight remains disabled until set_backlight() is
// called, so initialization is not visible as random frame-memory contents.
void init();

// Set the active-high BLK PWM duty cycle. The requested value is clamped to
// 0..100 percent.
void set_backlight(float percent);

// Clear the complete packed 4bpp software frame buffer to one grayscale level.
void clear(Gray4 gray);

// Draw into the software frame buffer. Coordinates outside the active area
// are clipped, allowing higher-level UI code to use these primitives safely.
// Writes are allowed while a frame is being presented. The in-flight frame may
// contain a mixture of old and new rows, which is accepted by this project.
void set_pixel(uint16_t x, uint16_t y, Gray4 gray);
void fill_circle(int16_t center_x, int16_t center_y, int16_t radius, Gray4 gray);

// Begin an asynchronous full-frame transfer. A DMA completion interrupt expands
// each packed grayscale row to the ST7789 RGB444 serial format and advances the
// transfer. Returns false when another frame is already being presented.
bool present();

// Return true until the DMA interrupt has completed the entire frame transfer.
bool is_presenting();

}  // namespace display
