// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/screen.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#include "types.h"
#include "display/ui/screen.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"
#include "gbp/terminal/receiver.h"

namespace Gbp::Terminal {
namespace {

constexpr Display::Gray4 BACKGROUND = 0x0F;
constexpr Display::Gray4 FOREGROUND = 0x00;
constexpr Display::Gray4 SHADES[4]{0x0F, 0x0A, 0x05, 0x00};

bool draw_centered_line(const std::string_view text, const int16 y) {
    const std::string translated = Display::Ui::translate(text);
    const int16 width = Display::Ui::measure_utf8(translated);
    if (width < 0 || width > Display::WIDTH) {
        return false;
    }
    return Display::Ui::draw_utf8(translated, static_cast<int16>((Display::WIDTH - width) / 2), y, FOREGROUND);
}

void draw_image(const uint8* data, const usize size, const uint8 palette) {
    Display::clear(BACKGROUND);
    const usize tile_rows = size / BYTES_PER_TILE_ROW;
    const usize image_height = tile_rows * 8;
    const usize first_row = image_height > Display::HEIGHT ? image_height - Display::HEIGHT : 0;
    const usize visible_height = std::min<usize>(image_height, Display::HEIGHT);
    const uint16 top = static_cast<uint16>((Display::HEIGHT - visible_height) / 2);
    const uint16 left = static_cast<uint16>((Display::WIDTH - 160) / 2);
    for (usize y = 0; y < visible_height; ++y) {
        const usize source_y = first_row + y;
        const usize tile_row = source_y / 8;
        const usize row_in_tile = source_y % 8;
        for (uint16 x = 0; x < 160; ++x) {
            const usize tile_offset = tile_row * BYTES_PER_TILE_ROW + (x / 8) * 16 + row_in_tile * 2;
            const uint8 mask = static_cast<uint8>(0x80u >> (x % 8));
            const uint8 color = static_cast<uint8>(
                ((data[tile_offset] & mask) ? 1u : 0u) | ((data[tile_offset + 1] & mask) ? 2u : 0u));
            const uint8 shade = static_cast<uint8>((palette >> (color * 2)) & 3u);
            Display::set_pixel(left + x, top + static_cast<uint16>(y), SHADES[shade]);
        }
    }
}

}  // namespace

bool draw_waiting() {
    return Display::Ui::draw_centered_text(Display::Ui::translate("Waiting"), BACKGROUND, FOREGROUND);
}

bool draw_receiving(const uint32 data_frames) {
    Display::clear(BACKGROUND);
    char count[16];
    std::snprintf(count, sizeof(count), "%lu", static_cast<unsigned long>(data_frames));
    return draw_centered_line("Receiving", 100) && draw_centered_line(count, 124);
}

void draw_preview(const uint8* data, const usize size, const uint8 palette) {
    draw_image(data, size, palette);
}

}  // namespace Gbp::Terminal
