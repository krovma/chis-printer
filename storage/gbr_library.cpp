// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "storage/gbr_library.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "gbp/terminal/receiver.h"

namespace storage {
namespace {

constexpr const char* DIRECTORY = "0:/DCIM";

bool is_gbr_name(const char* name) {
    const std::size_t length = std::strlen(name);
    if (length < 5 || name[length - 4] != '.') return false;
    const auto lower = [](const char ch) { return static_cast<char>(ch | 0x20); };
    return lower(name[length - 3]) == 'g' && lower(name[length - 2]) == 'b' &&
           lower(name[length - 1]) == 'r';
}

bool copy_path(const char* filename, char* path, const std::size_t capacity) {
    if (filename == nullptr || std::strchr(filename, '/') != nullptr || std::strchr(filename, '\\') != nullptr ||
        !is_gbr_name(filename)) return false;
    const int length = std::snprintf(path, capacity, "%s/%s", DIRECTORY, filename);
    return length > 0 && static_cast<std::size_t>(length) < capacity;
}

}  // namespace

bool GbrCatalog::refresh() {
    clear();
    DIR directory{};
    const FRESULT open_result = f_opendir(&directory, DIRECTORY);
    if (open_result == FR_NO_PATH) return true;
    if (open_result != FR_OK) {
        printf("ERROR: GBR catalog open: %d\n", static_cast<int>(open_result));
        return false;
    }
    FILINFO entry{};
    FRESULT result = FR_OK;
    while ((result = f_readdir(&directory, &entry)) == FR_OK && entry.fname[0] != '\0') {
        if ((entry.fattrib & AM_DIR) != 0 || !is_gbr_name(entry.fname)) continue;
        const std::size_t length = std::strlen(entry.fname);
        std::unique_ptr<NameNode> next{new (std::nothrow) NameNode};
        if (next) next->name.reset(new (std::nothrow) char[length + 1]);
        if (!next || !next->name) {
            result = FR_NOT_ENOUGH_CORE;
            break;
        }
        std::memcpy(next->name.get(), entry.fname, length + 1);
        NameNode* raw = next.get();
        if (tail_ == nullptr) names_ = std::move(next);
        else tail_->next = std::move(next);
        tail_ = raw;
        ++count_;
    }
    const FRESULT close_result = f_closedir(&directory);
    if (result != FR_OK || close_result != FR_OK) {
        printf("ERROR: GBR catalog scan: %d, close: %d\n", static_cast<int>(result), static_cast<int>(close_result));
        clear();
        return false;
    }
    return true;
}

void GbrCatalog::clear() {
    while (names_) {
        auto next = std::move(names_->next);
        names_ = std::move(next);
    }
    tail_ = nullptr;
    count_ = 0;
}

bool GbrCatalog::name_at(const std::size_t index, char* output, const std::size_t capacity) const {
    if (index >= count_ || output == nullptr || capacity == 0) return false;
    const NameNode* node = names_.get();
    for (std::size_t current = 0; current < index; ++current) node = node->next.get();
    const std::size_t length = std::strlen(node->name.get());
    if (length >= capacity) return false;
    std::memcpy(output, node->name.get(), length + 1);
    return true;
}

void GbrImage::clear() {
    if (open_) {
        f_close(&file_);
        open_ = false;
    }
    data_.reset();
    size_ = 0;
    capacity_ = 0;
    total_size_ = 0;
    block_count_ = 0;
    block_index_ = 0;
    first_block_offset_ = 0;
    multi_block_ = false;
    print_control_ = gbr::format::DEFAULT_PRINT_CONTROL;
}

namespace {

bool read_exact(FIL& file, std::uint8_t* output, const std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const UINT chunk = static_cast<UINT>(std::min<std::size_t>(size - offset, 1024));
        UINT read = 0;
        if (f_read(&file, output + offset, chunk, &read) != FR_OK || read != chunk) return false;
        offset += read;
    }
    return true;
}

std::size_t block_payload_size(const std::uint8_t* header) {
    if (header[0] != gbr::format::GBR_BLOCK_VERSION || header[1] != gbr::format::GBR_WIDTH) return 0;
    const std::uint16_t height = static_cast<std::uint16_t>(header[2] | (header[3] << 8));
    if (height == 0 || height % 8 != 0) return 0;
    const std::size_t size = static_cast<std::size_t>(gbr::format::GBR_WIDTH) * height / 4;
    return size <= gbp::terminal::IMAGE_CAPACITY ? size : 0;
}

}  // namespace

bool GbrImage::read_payload(const std::size_t size) {
    if (size > capacity_) {
        std::unique_ptr<std::uint8_t[]> buffer{new (std::nothrow) std::uint8_t[size]};
        if (!buffer) {
            printf("ERROR: GBR allocation failed: %lu bytes\n", static_cast<unsigned long>(size));
            return false;
        }
        data_ = std::move(buffer);
        capacity_ = size;
    }
    if (!read_exact(file_, data_.get(), size)) return false;
    size_ = size;
    return true;
}

bool GbrImage::read_block() {
    std::array<std::uint8_t, gbr::format::GBR_BLOCK_HEADER_SIZE> header{};
    if (!read_exact(file_, header.data(), header.size())) return false;
    const std::size_t payload_size = block_payload_size(header.data());
    if (payload_size == 0 || !read_payload(payload_size)) return false;
    for (std::size_t i = 0; i < print_control_.size(); ++i) print_control_[i] = header[4 + i];
    return true;
}

bool GbrImage::load(const char* filename) {
    clear();
    char path[GBR_NAME_CAPACITY + 9]{};
    if (!copy_path(filename, path, sizeof(path))) return false;
    const FRESULT open_result = f_open(&file_, path, FA_READ);
    if (open_result != FR_OK) {
        printf("ERROR: GBR open %s: %d\n", path, static_cast<int>(open_result));
        return false;
    }
    open_ = true;

    std::array<std::uint8_t, gbr::format::GBR_FILE_HEADER_SIZE> prefix{};
    bool valid = read_exact(file_, prefix.data(), prefix.size()) &&
                 std::memcmp(prefix.data(), "KGBR", 4) == 0;
    if (valid && prefix[4] == gbr::format::GBR_FILE_VERSION) {
        multi_block_ = true;
        block_count_ = prefix[5];
        first_block_offset_ = gbr::format::GBR_FILE_HEADER_SIZE;
        valid = block_count_ != 0;
        for (std::size_t index = 0; valid && index < block_count_; ++index) {
            std::array<std::uint8_t, gbr::format::GBR_BLOCK_HEADER_SIZE> header{};
            valid = read_exact(file_, header.data(), header.size());
            const std::size_t payload_size = valid ? block_payload_size(header.data()) : 0;
            const FSIZE_t next = file_.fptr + payload_size;
            valid = payload_size != 0 && next <= f_size(&file_) && f_lseek(&file_, next) == FR_OK;
            total_size_ += payload_size;
        }
        valid = valid && file_.fptr == f_size(&file_) &&
                f_lseek(&file_, first_block_offset_) == FR_OK && read_block();
    } else if (valid && (prefix[4] == 1 || prefix[4] == gbr::format::GBR_BLOCK_VERSION)) {
        block_count_ = 1;
        std::array<std::uint8_t, gbr::format::GBR_BLOCK_HEADER_SIZE> header{};
        header[0] = gbr::format::GBR_BLOCK_VERSION;
        header[1] = prefix[5];
        valid = read_exact(file_, header.data() + 2, 2);
        if (valid && prefix[4] == gbr::format::GBR_BLOCK_VERSION) {
            valid = read_exact(file_, header.data() + 4, 4);
        } else {
            for (std::size_t i = 0; i < 4; ++i) header[4 + i] = gbr::format::DEFAULT_PRINT_CONTROL[i];
        }
        const std::size_t payload_size = valid ? block_payload_size(header.data()) : 0;
        valid = payload_size != 0 && file_.fptr + payload_size == f_size(&file_) &&
                read_payload(payload_size);
        if (valid) {
            total_size_ = payload_size;
            for (std::size_t i = 0; i < print_control_.size(); ++i) print_control_[i] = header[4 + i];
        }
    } else {
        valid = false;
    }
    if (!valid) {
        printf("ERROR: invalid GBR file %s\n", path);
        clear();
    }
    return valid;
}

bool GbrImage::select_block(const std::size_t index) {
    if (!open_ || index >= block_count_) return false;
    if (index == block_index_) return true;
    if (!multi_block_ || f_lseek(&file_, first_block_offset_) != FR_OK) return false;
    for (std::size_t current = 0; current < index; ++current) {
        std::array<std::uint8_t, gbr::format::GBR_BLOCK_HEADER_SIZE> header{};
        if (!read_exact(file_, header.data(), header.size())) return false;
        const std::size_t payload_size = block_payload_size(header.data());
        if (payload_size == 0 || f_lseek(&file_, file_.fptr + payload_size) != FR_OK) return false;
    }
    if (!read_block()) return false;
    block_index_ = index;
    return true;
}

bool GbrImage::next_block() {
    if (!open_ || block_index_ + 1 >= block_count_ || !multi_block_) return false;
    if (!read_block()) return false;
    ++block_index_;
    return true;
}

}  // namespace storage
