// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "storage/sd_card.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

#include "types.h"
#include "config/toml_document.h"
#include "ff.h"
#include "hardware/gpio.h"
#include "pin_config.h"
#include "tf_card.h"
#include "tomlc17.h"

namespace Storage {
namespace {

constexpr const char* CONFIG_PATH = "0:/config.toml";
constexpr const char* CONFIG_TEMP_PATH = "0:/config.tmp";
constexpr const char* CONFIG_BACKUP_PATH = "0:/config.bak";
FATFS filesystem{};
bool mounted = false;
std::array<char, MAX_CONFIG_BYTES> config_buffer{};
std::array<char, MAX_CONFIG_BYTES> updated_buffer{};

enum class ReadResult { Ok, Missing, Error };

bool file_exists(const char* path) {
    FILINFO info{};
    return f_stat(path, &info) == FR_OK;
}

ReadResult read_file(const char* path, std::string_view& output) {
    FIL file{};
    const FRESULT open_result = f_open(&file, path, FA_READ);
    if (open_result == FR_NO_FILE) return ReadResult::Missing;
    if (open_result != FR_OK) return ReadResult::Error;
    const FSIZE_t size = f_size(&file);
    if (size == 0 || size > config_buffer.size()) {
        f_close(&file);
        return ReadResult::Error;
    }
    UINT bytes_read = 0;
    const FRESULT read_result = f_read(&file, config_buffer.data(), static_cast<UINT>(size), &bytes_read);
    const FRESULT close_result = f_close(&file);
    if (read_result != FR_OK || close_result != FR_OK || bytes_read != size) return ReadResult::Error;
    output = std::string_view{config_buffer.data(), bytes_read};
    return ReadResult::Ok;
}

bool valid_document(const std::string_view contents, const char* required_key = nullptr) {
    if (contents.size() > INT_MAX) return false;
    const toml_result_t document = toml_parse(contents.data(), static_cast<int>(contents.size()));
    if (!document.ok) {
        toml_free(document);
        return false;
    }
    const toml_datum_t version = toml_get(document.toptab, "format_version");
    const bool valid = version.type == TOML_INT64 && version.u.int64 == 1 &&
        (required_key == nullptr || toml_seek(document.toptab, required_key).type != TOML_UNKNOWN);
    toml_free(document);
    return valid;
}

bool valid_config_file(const char* path) {
    std::string_view contents;
    return read_file(path, contents) == ReadResult::Ok && valid_document(contents);
}

void recover_config_files() {
    const bool config_exists = file_exists(CONFIG_PATH);
    if (file_exists(CONFIG_BACKUP_PATH)) {
        if (!config_exists) {
            const FRESULT result = f_rename(CONFIG_BACKUP_PATH, CONFIG_PATH);
            if (result != FR_OK) printf("config.toml recovery failed: %d\n", static_cast<int>(result));
        } else if (valid_config_file(CONFIG_PATH)) {
            f_unlink(CONFIG_BACKUP_PATH);
        } else if (valid_config_file(CONFIG_BACKUP_PATH)) {
            f_unlink(CONFIG_TEMP_PATH);
            if (f_rename(CONFIG_PATH, CONFIG_TEMP_PATH) == FR_OK) {
                if (f_rename(CONFIG_BACKUP_PATH, CONFIG_PATH) == FR_OK) {
                    f_unlink(CONFIG_TEMP_PATH);
                } else {
                    f_rename(CONFIG_TEMP_PATH, CONFIG_PATH);
                }
            }
        }
    }
    if (file_exists(CONFIG_TEMP_PATH)) f_unlink(CONFIG_TEMP_PATH);
}

}  // namespace

bool mount_initialized_card() {
    if (gpio_get(Board::SD_CARD_DETECT_N)) {
        printf("SD card not detected\n");
        return false;
    }

    pico_fatfs_spi_config_t spi_config{
        Board::SD_SPI,
        Board::SD_SPI_INITIAL_BAUD,
        Board::SD_SPI_BAUD,
        Board::SD_MISO,
        Board::SD_CS_N,
        Board::SD_SCK,
        Board::SD_MOSI,
        true,
    };
    if (!pico_fatfs_set_config(&spi_config)) printf("SD card using PIO SPI\n");

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
    recover_config_files();
    return true;
}

bool read_config_text(std::string_view& output) {
    return mounted && read_file(CONFIG_PATH, output) == ReadResult::Ok;
}

bool write_config_value(const std::string_view path, const std::string_view value) {
    if (!mounted || path.size() >= 96) return false;
    std::string_view original;
    const ReadResult read_result = read_file(CONFIG_PATH, original);
    if (read_result == ReadResult::Error ||
        (read_result == ReadResult::Ok && !valid_document(original))) return false;
    if (read_result == ReadResult::Missing) original = {};

    usize updated_size = 0;
    if (!Config::update_toml_value(original, path, value, updated_buffer.data(),
                                   updated_buffer.size(), updated_size)) return false;
    char key[96]{};
    std::memcpy(key, path.data(), path.size());
    if (!valid_document(std::string_view{updated_buffer.data(), updated_size}, key)) return false;

    FIL output_file{};
    FRESULT result = f_open(&output_file, CONFIG_TEMP_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) return false;
    UINT bytes_written = 0;
    result = f_write(&output_file, updated_buffer.data(), static_cast<UINT>(updated_size), &bytes_written);
    const FRESULT sync_result = result == FR_OK && bytes_written == updated_size ? f_sync(&output_file) : result;
    const FRESULT close_result = f_close(&output_file);
    if (result != FR_OK || bytes_written != updated_size || sync_result != FR_OK || close_result != FR_OK) {
        f_unlink(CONFIG_TEMP_PATH);
        return false;
    }

    const bool had_config = read_result == ReadResult::Ok;
    if (had_config) {
        if (file_exists(CONFIG_BACKUP_PATH) && f_unlink(CONFIG_BACKUP_PATH) != FR_OK) {
            f_unlink(CONFIG_TEMP_PATH);
            return false;
        }
        result = f_rename(CONFIG_PATH, CONFIG_BACKUP_PATH);
        if (result != FR_OK) {
            f_unlink(CONFIG_TEMP_PATH);
            return false;
        }
    }
    result = f_rename(CONFIG_TEMP_PATH, CONFIG_PATH);
    if (result != FR_OK) {
        if (had_config) f_rename(CONFIG_BACKUP_PATH, CONFIG_PATH);
        f_unlink(CONFIG_TEMP_PATH);
        return false;
    }
    if (had_config) f_unlink(CONFIG_BACKUP_PATH);
    return true;
}

}  // namespace Storage
