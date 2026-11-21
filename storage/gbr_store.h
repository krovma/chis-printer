// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"
#include "ff.h"
#include "gbr/format.h"

namespace Storage {

// Core0-only, nonblocking at the file level: each step writes at most 1024
// bytes so the UI can redraw between FatFs calls.
class GbrStore {
  public:
    enum class StepResult : uint8 { Idle, Progress, Complete, Error };

    bool initialize();
    bool begin(const uint8* data, usize size, const Gbr::Format::PrintControl& print_control,
               bool append_to_previous = false);
    StepResult step();
    bool busy() const { return phase_ != Phase::Idle; }
    bool last_filename(char* output, usize capacity) const;

  private:
    enum class Phase : uint8 { Idle, FileHeader, BlockHeader, Payload, Count, Close };

    StepResult fail(const char* operation, FRESULT error);
    bool open_previous();
    bool rollback_append();

    FIL file_{};
    std::array<uint8, Gbr::Format::GBR_FILE_HEADER_SIZE> file_header_{};
    std::array<uint8, Gbr::Format::GBR_BLOCK_HEADER_SIZE> block_header_{};
    const uint8* data_ = nullptr;
    usize size_ = 0;
    usize offset_ = 0;
    uint64 next_number_ = 1;
    char path_[40]{};
    char last_path_[40]{};
    FSIZE_t previous_size_ = 0;
    uint8 block_count_ = 0;
    Phase phase_ = Phase::Idle;
    bool initialized_ = false;
    bool append_ = false;
    bool last_file_valid_ = false;
};

}  // namespace Storage
