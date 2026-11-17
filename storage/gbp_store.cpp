// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "storage/gbp_store.h"

#include <algorithm>
#include <cstdio>
#include <limits>

#include "gbp/terminal/receiver.h"
#include "storage/gbr_filename.h"

namespace storage {
namespace {

constexpr const char* IMAGE_DIRECTORY = "0:/DCIM";
constexpr UINT WRITE_CHUNK_SIZE = 1024;

}  // namespace

bool GbpStore::initialize() {
    DIR directory{};
    FRESULT result = f_opendir(&directory, IMAGE_DIRECTORY);
    if (result == FR_NO_PATH) {
        result = f_mkdir(IMAGE_DIRECTORY);
        if (result == FR_OK) {
            result = f_opendir(&directory, IMAGE_DIRECTORY);
        }
    }
    if (result != FR_OK) {
        printf("ERROR: DCIM open/create failed: %d\n", static_cast<int>(result));
        return false;
    }

    std::uint64_t largest = 0;
    FILINFO entry{};
    while ((result = f_readdir(&directory, &entry)) == FR_OK && entry.fname[0] != '\0') {
        std::uint64_t number = 0;
        if ((entry.fattrib & AM_DIR) == 0 && parse_gbr_filename(entry.fname, number)) {
            largest = std::max(largest, number);
        }
    }
    const FRESULT close_result = f_closedir(&directory);
    if (result != FR_OK || close_result != FR_OK || largest == std::numeric_limits<std::uint64_t>::max()) {
        printf("ERROR: DCIM scan failed: %d, close: %d\n", static_cast<int>(result), static_cast<int>(close_result));
        return false;
    }
    next_number_ = largest + 1;
    initialized_ = true;
    return true;
}

bool GbpStore::begin(const std::uint8_t* data, const std::size_t size,
                     const gbp::format::PrintControl& print_control) {
    if (!initialized_ || busy() || data == nullptr || size < gbp::terminal::BYTES_PER_TILE_ROW ||
        size % gbp::terminal::BYTES_PER_TILE_ROW != 0 || size > gbp::terminal::IMAGE_CAPACITY) {
        printf("ERROR: invalid GBR image or writer state\n");
        return false;
    }

    const std::size_t height = size / gbp::terminal::BYTES_PER_TILE_ROW * 8;
    if (height > std::numeric_limits<std::uint16_t>::max()) {
        printf("ERROR: GBR image height exceeds 16 bits\n");
        return false;
    }

    FRESULT result = FR_EXIST;
    while (next_number_ != std::numeric_limits<std::uint64_t>::max()) {
        if (!format_gbr_path(next_number_++, path_, sizeof(path_))) {
            break;
        }
        result = f_open(&file_, path_, FA_CREATE_NEW | FA_WRITE);
        if (result != FR_EXIST) {
            break;
        }
    }
    if (result != FR_OK) {
        printf("ERROR: GBR file create failed: %d\n", static_cast<int>(result));
        return false;
    }

    header_ = gbp::format::make_gbr_header(gbp::format::GBR_WIDTH, static_cast<std::uint16_t>(height),
                                           print_control);
    data_ = data;
    size_ = size;
    offset_ = 0;
    phase_ = Phase::Header;
    return true;
}

GbpStore::StepResult GbpStore::fail(const char* operation, const FRESULT error) {
    printf("ERROR: GBR %s failed for %s: %d\n", operation, path_, static_cast<int>(error));
    f_close(&file_);
    const FRESULT unlink_result = f_unlink(path_);
    if (unlink_result != FR_OK) {
        printf("ERROR: partial GBR cleanup failed for %s: %d\n", path_, static_cast<int>(unlink_result));
    }
    phase_ = Phase::Idle;
    return StepResult::Error;
}

GbpStore::StepResult GbpStore::step() {
    UINT written = 0;
    switch (phase_) {
        case Phase::Idle:
            return StepResult::Idle;
        case Phase::Header: {
            const FRESULT result = f_write(&file_, header_.data(), static_cast<UINT>(header_.size()), &written);
            if (result != FR_OK || written != header_.size()) {
                return fail("header write", result);
            }
            phase_ = Phase::Payload;
            return StepResult::Progress;
        }
        case Phase::Payload: {
            const UINT count = static_cast<UINT>(std::min<std::size_t>(size_ - offset_, WRITE_CHUNK_SIZE));
            const FRESULT result = f_write(&file_, data_ + offset_, count, &written);
            if (result != FR_OK || written != count) {
                return fail("payload write", result);
            }
            offset_ += written;
            if (offset_ == size_) {
                phase_ = Phase::Close;
            }
            return StepResult::Progress;
        }
        case Phase::Close: {
            const FRESULT result = f_close(&file_);
            phase_ = Phase::Idle;
            if (result != FR_OK) {
                printf("ERROR: GBR close failed for %s: %d\n", path_, static_cast<int>(result));
                const FRESULT unlink_result = f_unlink(path_);
                if (unlink_result != FR_OK) {
                    printf("ERROR: partial GBR cleanup failed for %s: %d\n", path_, static_cast<int>(unlink_result));
                }
                return StepResult::Error;
            }
            printf("Saved %s\n", path_);
            return StepResult::Complete;
        }
    }
    return StepResult::Error;
}

}  // namespace storage
