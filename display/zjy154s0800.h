// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "display/palette.h"

namespace display {

inline constexpr uint16_t WIDTH             = 240;
inline constexpr uint16_t HEIGHT            = 240;
inline constexpr size_t   FRAME_BUFFER_SIZE = static_cast<size_t>(WIDTH) * HEIGHT / 2;
inline constexpr std::array<uint8_t, 5> BACKLIGHT_DUTY_PERCENT{10, 25, 50, 75, 100};
inline constexpr uint8_t                DEFAULT_BACKLIGHT_KEY = 2;
static_assert(BACKLIGHT_DUTY_PERCENT[DEFAULT_BACKLIGHT_KEY] == 50);

// Four-bit grayscale level. The selected palette tints it during presentation;
// the default white palette produces RGB444 0x000, 0x111, ... 0xFFF.
using Gray4 = uint8_t;

// Initialize the ZJY154S0800TG02 module with the vendor-provided ST7789VW
// register sequence. The backlight stays off until the first frame finishes.
void init();

// Set the active-high BLK PWM duty cycle directly, clamped to 0..100 percent.
// This overrides the selected preset until set_backlight_key() is called.
// Before the first frame, the value is stored and applied when transfer ends.
void set_backlight(float percent);

// Keys 0..4 select the five preset duty cycles above. Key 2 (50%) is the
// default. An invalid key is rejected without changing the current setting.
// Before the first complete frame, the preset is stored while BLK stays off.
bool set_backlight_key(uint8_t key);
uint8_t backlight_key();

// Keys 0..5 select hues 0..300 degrees in 60-degree steps; key 6 selects
// neutral white. Invalid keys are rejected without changing the selection.
// A successful change takes effect on the next accepted present() call.
bool set_palette_key(uint8_t key);
uint8_t palette_key();

// Clear the complete packed 4bpp software frame buffer to one grayscale level.
void clear(Gray4 gray);

// Draw into the software frame buffer. Coordinates outside the active area
// are clipped, allowing higher-level UI code to use these primitives safely.
// Writes are allowed while a frame is being presented. The in-flight frame may
// contain a mixture of old and new rows, which is accepted by this project.
void set_pixel(uint16_t x, uint16_t y, Gray4 gray);
void fill_circle(int16_t center_x, int16_t center_y, int16_t radius, Gray4 gray);

// Begin an asynchronous full-frame transfer. A DMA completion interrupt expands
// each packed grayscale row through the selected palette to the ST7789 RGB444
// serial format and advances the transfer. Returns false when another frame is
// already being presented.
bool present();

// Return true until the DMA interrupt has completed the entire frame transfer.
bool is_presenting();

}  // namespace display
