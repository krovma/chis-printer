// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>

#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/types.h"

namespace board {

// MP2637 status outputs (open-drain, active-low).
inline constexpr uint MP_BOOST_N  = 0;
inline constexpr uint MP_ACOK_N   = 1;
inline constexpr uint MP_CHARGE_N = 2;

// CW2015 fuel gauge.
inline constexpr uint     CW_ALERT_N             = 3;
inline constexpr uint     CW_SDA                 = 8;
inline constexpr uint     CW_SCL                 = 9;
inline i2c_inst_t* const  FUEL_GAUGE_I2C         = i2c0;
inline constexpr uint32_t FUEL_GAUGE_I2C_BAUD    = 100'000;
inline constexpr uint8_t  FUEL_GAUGE_I2C_ADDRESS = 0x62;

// microSD card (SPI0).
inline constexpr uint     SD_MISO             = 4;
inline constexpr uint     SD_CS_N             = 5;
inline constexpr uint     SD_SCK              = 6;
inline constexpr uint     SD_MOSI             = 7;
inline constexpr uint     SD_CARD_DETECT_N    = 22;
inline spi_inst_t* const  SD_SPI              = spi0;
inline constexpr uint32_t SD_SPI_INITIAL_BAUD = 400'000;
inline constexpr uint32_t SD_SPI_BAUD         = 12'000'000;

// ST7789 display (SPI1, write-only).
inline constexpr uint     TFT_SCK       = 10;
inline constexpr uint     TFT_MOSI      = 11;
inline constexpr uint     TFT_DC        = 12;
inline constexpr uint     TFT_CS_N      = 13;
inline constexpr uint     TFT_BACKLIGHT = 14;
inline constexpr uint     TFT_RESET_N   = 21;
inline spi_inst_t* const  TFT_SPI       = spi1;
// The ZJY module timing table leaves the serial limits as TBD. Use a
// conservative initial clock until signal integrity is measured on hardware.
inline constexpr uint32_t TFT_SPI_BAUD  = 20'000'000;

// GP15 is deliberately left unused by the current hardware revision.
inline constexpr uint RESERVED_GP15 = 15;

// Five-way switch (active-low).
inline constexpr uint KEY_RIGHT_N   = 16;
inline constexpr uint KEY_DOWN_N    = 17;
inline constexpr uint KEY_CONFIRM_N = 18;
inline constexpr uint KEY_LEFT_N    = 19;
inline constexpr uint KEY_UP_N      = 20;

// Game Boy link, named from this board's point of view.
inline constexpr uint LINK_DATA_IN  = 26;
inline constexpr uint LINK_CLOCK    = 27;
inline constexpr uint LINK_DATA_OUT = 28;

}  // namespace board
