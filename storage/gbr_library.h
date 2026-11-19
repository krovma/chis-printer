// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "ff.h"
#include "gbr/format.h"

namespace storage {

inline constexpr std::size_t GBR_NAME_CAPACITY = FF_LFN_BUF + 1;

class GbrCatalog {
  public:
    ~GbrCatalog() { clear(); }
    bool refresh();
    void clear();
    std::size_t count() const { return count_; }
    bool name_at(std::size_t index, char* output, std::size_t capacity) const;

  private:
    struct NameNode {
        std::unique_ptr<char[]> name;
        std::unique_ptr<NameNode> next;
    };

    std::unique_ptr<NameNode> names_{};
    NameNode* tail_ = nullptr;
    std::size_t count_ = 0;
};

class GbrImage {
  public:
    ~GbrImage() { clear(); }
    bool load(const char* filename);
    bool select_block(std::size_t index);
    bool next_block();
    void clear();

    const std::uint8_t* data() const { return data_.get(); }
    std::size_t size() const { return size_; }
    std::size_t total_size() const { return total_size_; }
    std::size_t block_count() const { return block_count_; }
    std::size_t block_index() const { return block_index_; }
    std::uint8_t palette() const { return print_control_[2]; }
    const gbr::format::PrintControl& print_control() const { return print_control_; }

  private:
    bool read_block();
    bool read_payload(std::size_t size);

    FIL file_{};
    bool open_ = false;
    std::unique_ptr<std::uint8_t[]> data_{};
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::size_t total_size_ = 0;
    std::size_t block_count_ = 0;
    std::size_t block_index_ = 0;
    FSIZE_t first_block_offset_ = 0;
    bool multi_block_ = false;
    gbr::format::PrintControl print_control_ = gbr::format::DEFAULT_PRINT_CONTROL;
};

}  // namespace storage
