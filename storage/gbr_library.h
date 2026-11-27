// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <memory>

#include "types.h"
#include "ff.h"
#include "gbr/format.h"

namespace Storage {

inline constexpr usize GBR_NAME_CAPACITY = FF_LFN_BUF + 1;

// Scans the SD directory and owns the filenames shown in the file list.
class GbrCatalog {
  public:
    ~GbrCatalog() { clear(); }
    bool refresh();
    void clear();
    usize count() const { return count_; }
    bool name_at(usize index, char* output, usize capacity) const;

  private:
    struct NameNode {
        std::unique_ptr<char[]> name;
        std::unique_ptr<NameNode> next;
    };

    std::unique_ptr<NameNode> names_{};
    NameNode* tail_ = nullptr;
    usize count_ = 0;
};

// Validates a GBR file and loads one block at a time into a reusable buffer.
class GbrImage {
  public:
    ~GbrImage() { clear(); }
    bool load(const char* filename);
    bool select_block(usize index);
    bool next_block();
    void clear();

    const uint8* data() const { return data_.get(); }
    usize size() const { return size_; }
    usize total_size() const { return total_size_; }
    usize source_rows() const { return source_rows_; }
    usize inner_margin_rows() const { return inner_margin_rows_; }
    usize print_rows() const { return source_rows_ + inner_margin_rows_ + first_margin_rows_ + last_margin_rows_; }
    usize block_count() const { return block_count_; }
    usize block_index() const { return block_index_; }
    uint8 palette() const { return print_control_[2]; }
    const Gbr::Format::PrintControl& print_control() const { return print_control_; }

  private:
    bool read_block();
    bool read_payload(usize size);

    FIL file_{};
    bool open_ = false;
    std::unique_ptr<uint8[]> data_{};
    usize size_ = 0;
    usize capacity_ = 0;
    usize total_size_ = 0;
    usize source_rows_ = 0;
    usize inner_margin_rows_ = 0;
    usize first_margin_rows_ = 0;
    usize last_margin_rows_ = 0;
    usize block_count_ = 0;
    usize block_index_ = 0;
    FSIZE_t first_block_offset_ = 0;
    bool multi_block_ = false;
    Gbr::Format::PrintControl print_control_ = Gbr::Format::DEFAULT_PRINT_CONTROL;
};

}  // namespace Storage
