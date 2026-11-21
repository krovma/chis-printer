// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "storage/gbr_store.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "types.h"
#include "gbp/terminal/receiver.h"
#include "storage/gbr_filename.h"

namespace Storage {
namespace {

constexpr const char* IMAGE_DIRECTORY = "0:/DCIM";
constexpr UINT WRITE_CHUNK_SIZE = 1024;
constexpr FSIZE_t BLOCK_COUNT_OFFSET = 5;

}  // namespace

bool GbrStore::initialize() {
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

    uint64 largest = 0;
    FILINFO entry{};
    while ((result = f_readdir(&directory, &entry)) == FR_OK && entry.fname[0] != '\0') {
        uint64 number = 0;
        if ((entry.fattrib & AM_DIR) == 0 && parse_gbr_filename(entry.fname, number)) {
            largest = std::max(largest, number);
        }
    }
    const FRESULT close_result = f_closedir(&directory);
    if (result != FR_OK || close_result != FR_OK || largest == std::numeric_limits<uint64>::max()) {
        printf("ERROR: DCIM scan failed: %d, close: %d\n", static_cast<int>(result), static_cast<int>(close_result));
        return false;
    }
    next_number_ = largest + 1;
    last_file_valid_ = false;
    initialized_ = true;
    return true;
}

bool GbrStore::last_filename(char* output, const usize capacity) const {
    if (!last_file_valid_ || output == nullptr || capacity == 0) {
        return false;
    }
    const char* name = std::strrchr(last_path_, '/');
    if (name == nullptr) {
        return false;
    }
    ++name;
    const usize length = std::strlen(name);
    if (length == 0 || length >= capacity) {
        return false;
    }
    std::memcpy(output, name, length + 1);
    return true;
}

bool GbrStore::open_previous() {
    if (!last_file_valid_) {
        return false;
    }
    std::memcpy(path_, last_path_, sizeof(path_));
    const FRESULT open_result = f_open(&file_, path_, FA_READ | FA_WRITE);
    if (open_result != FR_OK) {
        printf("ERROR: GBR reopen %s: %d\n", path_, static_cast<int>(open_result));
        last_file_valid_ = false;
        return false;
    }
    std::array<uint8, Gbr::Format::GBR_FILE_HEADER_SIZE> header{};
    UINT read = 0;
    const FRESULT read_result = f_read(&file_, header.data(), static_cast<UINT>(header.size()), &read);
    const bool valid = read_result == FR_OK && read == header.size() &&
                       header == Gbr::Format::make_gbr_file_header(block_count_) &&
                       f_size(&file_) == previous_size_;
    const FRESULT seek_result = valid ? f_lseek(&file_, previous_size_) : FR_INVALID_OBJECT;
    if (!valid || seek_result != FR_OK) {
        printf("ERROR: previous GBR changed or unreadable: %s\n", path_);
        f_close(&file_);
        last_file_valid_ = false;
        return false;
    }
    return true;
}

bool GbrStore::begin(const uint8* data, const usize size,
                     const Gbr::Format::PrintControl& print_control, const bool append_to_previous) {
    if (!initialized_ || busy() || data == nullptr || size < Gbp::Terminal::BYTES_PER_TILE_ROW ||
        size % Gbp::Terminal::BYTES_PER_TILE_ROW != 0 || size > Gbp::Terminal::IMAGE_CAPACITY) {
        printf("ERROR: invalid GBR image or writer state\n");
        return false;
    }
    const usize height = size / Gbp::Terminal::BYTES_PER_TILE_ROW * 8;
    if (height > std::numeric_limits<uint16>::max()) {
        printf("ERROR: GBR image height exceeds 16 bits\n");
        return false;
    }

    // A repeated print group extends only the last unchanged GBR file.
    append_ = append_to_previous && open_previous();
    if (!append_) {
        last_file_valid_ = false;
        FRESULT result = FR_EXIST;
        while (next_number_ != std::numeric_limits<uint64>::max()) {
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
        previous_size_ = 0;
        block_count_ = 0;
    }

    ++block_count_;  // The v3 count is one byte; overflow handling is intentionally deferred.
    file_header_ = Gbr::Format::make_gbr_file_header(block_count_);
    block_header_ = Gbr::Format::make_gbr_block_header(Gbr::Format::GBR_WIDTH,
                                                       static_cast<uint16>(height), print_control);
    data_ = data;
    size_ = size;
    offset_ = 0;
    phase_ = append_ ? Phase::BlockHeader : Phase::FileHeader;
    return true;
}

bool GbrStore::rollback_append() {
    // Restore both the block count and the original file length after a failed append.
    UINT written = 0;
    const uint8 old_count = static_cast<uint8>(block_count_ - 1);
    bool ok = f_lseek(&file_, BLOCK_COUNT_OFFSET) == FR_OK &&
              f_write(&file_, &old_count, 1, &written) == FR_OK && written == 1;
    if (f_lseek(&file_, previous_size_) != FR_OK || f_truncate(&file_) != FR_OK) {
        ok = false;
    }
    if (f_close(&file_) != FR_OK) {
        ok = false;
    }
    if (!ok) {
        printf("ERROR: GBR append rollback failed for %s\n", path_);
    }
    last_file_valid_ = ok;
    block_count_ = old_count;
    return ok;
}

GbrStore::StepResult GbrStore::fail(const char* operation, const FRESULT error) {
    printf("ERROR: GBR %s failed for %s: %d\n", operation, path_, static_cast<int>(error));
    if (append_) {
        rollback_append();
    } else {
        f_close(&file_);
        const FRESULT unlink_result = f_unlink(path_);
        if (unlink_result != FR_OK) {
            printf("ERROR: partial GBR cleanup failed for %s: %d\n", path_, static_cast<int>(unlink_result));
        }
    }
    phase_ = Phase::Idle;
    return StepResult::Error;
}

GbrStore::StepResult GbrStore::step() {
    UINT written = 0;
    switch (phase_) {
        case Phase::Idle:
            return StepResult::Idle;
        case Phase::FileHeader: {
            const FRESULT result = f_write(&file_, file_header_.data(), static_cast<UINT>(file_header_.size()), &written);
            if (result != FR_OK || written != file_header_.size()) {
                return fail("file header write", result);
            }
            phase_ = Phase::BlockHeader;
            return StepResult::Progress;
        }
        case Phase::BlockHeader: {
            const FRESULT result = f_write(&file_, block_header_.data(), static_cast<UINT>(block_header_.size()), &written);
            if (result != FR_OK || written != block_header_.size()) {
                return fail("block header write", result);
            }
            phase_ = Phase::Payload;
            return StepResult::Progress;
        }
        case Phase::Payload: {
            const UINT count = static_cast<UINT>(std::min<usize>(size_ - offset_, WRITE_CHUNK_SIZE));
            const FRESULT result = f_write(&file_, data_ + offset_, count, &written);
            if (result != FR_OK || written != count) {
                return fail("payload write", result);
            }
            offset_ += written;
            if (offset_ == size_) {
                phase_ = append_ ? Phase::Count : Phase::Close;
            }
            return StepResult::Progress;
        }
        case Phase::Count: {
            // Update the v3 file header only after the new block is complete.
            const FRESULT seek_result = f_lseek(&file_, BLOCK_COUNT_OFFSET);
            if (seek_result != FR_OK) {
                return fail("block count seek", seek_result);
            }
            const FRESULT result = f_write(&file_, &block_count_, 1, &written);
            if (result != FR_OK || written != 1) {
                return fail("block count write", result);
            }
            phase_ = Phase::Close;
            return StepResult::Progress;
        }
        case Phase::Close: {
            const FRESULT result = f_close(&file_);
            phase_ = Phase::Idle;
            if (result != FR_OK) {
                printf("ERROR: GBR close failed for %s: %d\n", path_, static_cast<int>(result));
                if (!append_) {
                    f_unlink(path_);
                }
                last_file_valid_ = false;
                return StepResult::Error;
            }
            previous_size_ += size_ + block_header_.size() + (append_ ? 0 : file_header_.size());
            std::memcpy(last_path_, path_, sizeof(last_path_));
            last_file_valid_ = true;
            printf("Saved %s (%u block%s)\n", path_, static_cast<unsigned>(block_count_),
                   block_count_ == 1 ? "" : "s");
            return StepResult::Complete;
        }
    }
    return StepResult::Error;
}

}  // namespace Storage
