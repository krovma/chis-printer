// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>

#include "types.h"

namespace Gbr::Format {

// KGBR v3 is a six-byte file header followed by one or more v2 block headers
// and their uncompressed 2bpp tile streams. Each 8x8 tile is 16 bytes, with
// tiles ordered left-to-right in 8-pixel-high rows. Height is little-endian.
using PrintControl = std::array<uint8, 4>;
inline constexpr PrintControl DEFAULT_PRINT_CONTROL{1, 0x13, 0xE4, 0x40};
inline constexpr uint8 GBR_FILE_VERSION = 3;
inline constexpr uint8 GBR_BLOCK_VERSION = 2;
inline constexpr uint8 GBR_WIDTH = 160;
inline constexpr usize GBR_FILE_HEADER_SIZE = 6;
inline constexpr usize GBR_BLOCK_HEADER_SIZE = 8;
inline constexpr usize GBR_V2_HEADER_SIZE = 12;

constexpr uint8 preview_palette(const uint8 raw) {
    if (raw == 0) {
        return 0xE4;
    }
    if (raw == 0xE1) {
        return 0xD2;
    }
    if (raw == 0x2D) {
        return 0x1E;
    }
    return raw;
}

constexpr std::array<uint8, GBR_FILE_HEADER_SIZE> make_gbr_file_header(const uint8 blocks) {
    return {'K', 'G', 'B', 'R', GBR_FILE_VERSION, blocks};
}

constexpr std::array<uint8, GBR_BLOCK_HEADER_SIZE> make_gbr_block_header(
    const uint8 width, const uint16 height, const PrintControl& print_control) {
    return {GBR_BLOCK_VERSION, width,
            static_cast<uint8>(height), static_cast<uint8>(height >> 8),
            print_control[0], print_control[1], print_control[2], print_control[3]};
}

// Kept for reading and testing older standalone v2 files.
constexpr std::array<uint8, GBR_V2_HEADER_SIZE> make_gbr_header(
    const uint8 width, const uint16 height, const PrintControl& print_control) {
    const auto block = make_gbr_block_header(width, height, print_control);
    return {'K', 'G', 'B', 'R', block[0], block[1], block[2], block[3],
            block[4], block[5], block[6], block[7]};
}

constexpr PrintControl m2_print_control(PrintControl control) {
    control[0] = 1;
    return control;
}

}  // namespace Gbr::Format
