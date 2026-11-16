// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "storage/sd_card.h"

#include <array>
#include <cstdio>
#include <string_view>

#include "ff.h"
#include "hardware/gpio.h"
#include "pin_config.h"
#include "tf_card.h"

namespace storage {
namespace {

constexpr std::size_t MAX_CONFIG_BYTES = 4096;
FATFS filesystem{};
bool mounted = false;
std::array<char, MAX_CONFIG_BYTES> config_buffer{};

}  // namespace

bool mount_initialized_card() {
    if (gpio_get(board::SD_CARD_DETECT_N)) {
        printf("SD card not detected\n");
        return false;
    }

    pico_fatfs_spi_config_t spi_config{
        board::SD_SPI,
        board::SD_SPI_INITIAL_BAUD,
        board::SD_SPI_BAUD,
        board::SD_MISO,
        board::SD_CS_N,
        board::SD_SCK,
        board::SD_MOSI,
        true,
    };
    if (!pico_fatfs_set_config(&spi_config)) {
        printf("SD card using PIO SPI\n");
    }

    const FRESULT mount_result = f_mount(&filesystem, "0:", 1);
    if (mount_result != FR_OK) {
        printf("SD mount failed: %d\n", static_cast<int>(mount_result));
        return false;
    }

    FIL marker{};
    const FRESULT open_result = f_open(&marker, "0:/.kproot", FA_READ);
    if (open_result != FR_OK) {
        printf("SD root marker missing or unreadable: %d\n", static_cast<int>(open_result));
        f_unmount("0:");
        return false;
    }

    const FRESULT close_result = f_close(&marker);
    if (close_result != FR_OK) {
        printf("SD root marker close failed: %d\n", static_cast<int>(close_result));
        f_unmount("0:");
        return false;
    }

    mounted = true;
    return true;
}

bool load_display_config(config::DisplayConfig& output) {
    if (!mounted) {
        return false;
    }

    FIL file{};
    const FRESULT open_result = f_open(&file, "0:/config.toml", FA_READ);
    if (open_result != FR_OK) {
        printf("config.toml open failed: %d\n", static_cast<int>(open_result));
        return false;
    }

    const FSIZE_t file_size = f_size(&file);
    if (file_size == 0 || file_size > config_buffer.size()) {
        printf("config.toml size invalid\n");
        f_close(&file);
        return false;
    }

    UINT bytes_read = 0;
    const FRESULT read_result = f_read(&file, config_buffer.data(), static_cast<UINT>(file_size), &bytes_read);
    const FRESULT close_result = f_close(&file);
    if (read_result != FR_OK || close_result != FR_OK || bytes_read != file_size) {
        printf("config.toml read failed\n");
        return false;
    }

    const bool parsed = config::parse_display_config(
        std::string_view{config_buffer.data(), static_cast<std::size_t>(bytes_read)}, output);
    if (!parsed) {
        printf("config.toml display settings invalid\n");
    }
    return parsed;
}

}  // namespace storage
