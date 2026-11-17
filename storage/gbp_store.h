// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ff.h"
#include "gbp/format/gbr.h"

namespace storage {

// Core0-only, nonblocking at the file level: each step writes at most 1024
// bytes so the UI can redraw between FatFs calls.
class GbpStore {
  public:
    enum class StepResult : std::uint8_t { Idle, Progress, Complete, Error };

    bool initialize();
    bool begin(const std::uint8_t* data, std::size_t size, const gbp::format::PrintControl& print_control);
    StepResult step();
    bool busy() const { return phase_ != Phase::Idle; }

  private:
    enum class Phase : std::uint8_t { Idle, Header, Payload, Close };

    StepResult fail(const char* operation, FRESULT error);

    FIL file_{};
    std::array<std::uint8_t, gbp::format::GBR_HEADER_SIZE> header_{};
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
    std::uint64_t next_number_ = 1;
    char path_[40]{};
    Phase phase_ = Phase::Idle;
    bool initialized_ = false;
};

}  // namespace storage
