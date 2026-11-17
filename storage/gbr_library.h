// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "ff.h"
#include "gbp/format/gbr.h"

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
    bool load(const char* filename);
    void clear();

    const std::uint8_t* data() const { return data_.get(); }
    std::size_t size() const { return size_; }
    std::uint8_t palette() const { return print_control_[2]; }
    const gbp::format::PrintControl& print_control() const { return print_control_; }

  private:
    std::unique_ptr<std::uint8_t[]> data_{};
    std::size_t size_ = 0;
    gbp::format::PrintControl print_control_ = gbp::format::DEFAULT_PRINT_CONTROL;
};

}  // namespace storage
