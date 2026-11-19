// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ff.h"
#include "gbr/format.h"

namespace storage {

// Core0-only, nonblocking at the file level: each step writes at most 1024
// bytes so the UI can redraw between FatFs calls.
class GbrStore {
  public:
    enum class StepResult : std::uint8_t { Idle, Progress, Complete, Error };

    bool initialize();
    bool begin(const std::uint8_t* data, std::size_t size, const gbr::format::PrintControl& print_control,
               bool append_to_previous = false);
    StepResult step();
    bool busy() const { return phase_ != Phase::Idle; }

  private:
    enum class Phase : std::uint8_t { Idle, FileHeader, BlockHeader, Payload, Count, Close };

    StepResult fail(const char* operation, FRESULT error);
    bool open_previous();
    bool rollback_append();

    FIL file_{};
    std::array<std::uint8_t, gbr::format::GBR_FILE_HEADER_SIZE> file_header_{};
    std::array<std::uint8_t, gbr::format::GBR_BLOCK_HEADER_SIZE> block_header_{};
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
    std::uint64_t next_number_ = 1;
    char path_[40]{};
    char last_path_[40]{};
    FSIZE_t previous_size_ = 0;
    std::uint8_t block_count_ = 0;
    Phase phase_ = Phase::Idle;
    bool initialized_ = false;
    bool append_ = false;
    bool last_file_valid_ = false;
};

}  // namespace storage
