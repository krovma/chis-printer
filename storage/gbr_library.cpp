// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "storage/gbr_library.h"

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
    data_.reset();
    size_ = 0;
    print_control_ = gbp::format::DEFAULT_PRINT_CONTROL;
}

bool GbrImage::load(const char* filename) {
    clear();
    char path[GBR_NAME_CAPACITY + 9]{};
    if (!copy_path(filename, path, sizeof(path))) return false;
    FIL file{};
    const FRESULT open_result = f_open(&file, path, FA_READ);
    if (open_result != FR_OK) {
        printf("ERROR: GBR open %s: %d\n", path, static_cast<int>(open_result));
        return false;
    }

    std::array<std::uint8_t, gbp::format::GBR_HEADER_SIZE> header{};
    UINT read = 0;
    const FRESULT header_result = f_read(&file, header.data(), static_cast<UINT>(header.size()), &read);
    const bool v1 = read >= 8 && header[4] == 1;
    const bool v2 = read >= header.size() && header[4] == gbp::format::GBR_VERSION;
    const std::size_t header_size = v1 ? 8 : header.size();
    const std::uint16_t height = static_cast<std::uint16_t>(header[6] | (header[7] << 8));
    const std::size_t payload_size = static_cast<std::size_t>(header[5]) * height / 4;
    const bool valid = header_result == FR_OK && std::memcmp(header.data(), "KGBR", 4) == 0 &&
                       (v1 || v2) && header[5] == gbp::format::GBR_WIDTH &&
                       height != 0 && height % 8 == 0 &&
                       payload_size <= gbp::terminal::IMAGE_CAPACITY &&
                       f_size(&file) == header_size + payload_size;
    if (!valid) {
        printf("ERROR: invalid GBR file %s\n", path);
        f_close(&file);
        return false;
    }
    if (v1 && f_lseek(&file, 8) != FR_OK) {
        f_close(&file);
        return false;
    }

    std::unique_ptr<std::uint8_t[]> buffer{new (std::nothrow) std::uint8_t[payload_size]};
    if (!buffer) {
        printf("ERROR: GBR allocation failed: %lu bytes\n", static_cast<unsigned long>(payload_size));
        f_close(&file);
        return false;
    }
    std::size_t offset = 0;
    while (offset < payload_size) {
        const UINT length = static_cast<UINT>((payload_size - offset) > 1024 ? 1024 : payload_size - offset);
        read = 0;
        const FRESULT result = f_read(&file, buffer.get() + offset, length, &read);
        if (result != FR_OK || read != length) {
            printf("ERROR: GBR read %s: %d\n", path, static_cast<int>(result));
            f_close(&file);
            return false;
        }
        offset += read;
    }
    const FRESULT close_result = f_close(&file);
    if (close_result != FR_OK) return false;
    if (v2) {
        for (std::size_t i = 0; i < print_control_.size(); ++i) print_control_[i] = header[8 + i];
    }
    data_ = std::move(buffer);
    size_ = payload_size;
    return true;
}

}  // namespace storage
