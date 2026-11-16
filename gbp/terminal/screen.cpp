// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/screen.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string_view>

#include "display/ui/screen.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"

namespace gbp::terminal {
namespace {

constexpr display::Gray4 BACKGROUND = 0x0F;
constexpr display::Gray4 FOREGROUND = 0x00;
constexpr display::Gray4 SHADES[4]{0x0F, 0x0A, 0x05, 0x00};

bool draw_centered_line(const std::string_view text, const std::int16_t y) {
    const std::int16_t width = display::ui::measure_utf8(text);
    if (width < 0 || width > display::WIDTH) {
        return false;
    }
    return display::ui::draw_utf8(text, static_cast<std::int16_t>((display::WIDTH - width) / 2), y, FOREGROUND);
}

void draw_image(const Receiver& receiver) {
    display::clear(BACKGROUND);
    const std::size_t tile_rows = receiver.image_size() / BYTES_PER_TILE_ROW;
    const std::size_t image_height = tile_rows * 8;
    const std::size_t first_row = image_height > display::HEIGHT ? image_height - display::HEIGHT : 0;
    const std::size_t visible_height = std::min<std::size_t>(image_height, display::HEIGHT);
    const std::uint16_t top = static_cast<std::uint16_t>((display::HEIGHT - visible_height) / 2);
    const std::uint16_t left = static_cast<std::uint16_t>((display::WIDTH - 160) / 2);
    const auto* bytes = receiver.image_data();

    for (std::size_t y = 0; y < visible_height; ++y) {
        const std::size_t source_y = first_row + y;
        const std::size_t tile_row = source_y / 8;
        const std::size_t row_in_tile = source_y % 8;
        for (std::uint16_t x = 0; x < 160; ++x) {
            const std::size_t tile_offset = tile_row * BYTES_PER_TILE_ROW + (x / 8) * 16 + row_in_tile * 2;
            const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (x % 8));
            const std::uint8_t color = static_cast<std::uint8_t>(
                ((bytes[tile_offset] & mask) ? 1u : 0u) | ((bytes[tile_offset + 1] & mask) ? 2u : 0u));
            const std::uint8_t shade = static_cast<std::uint8_t>((receiver.palette() >> (color * 2)) & 3u);
            display::set_pixel(left + x, top + static_cast<std::uint16_t>(y), SHADES[shade]);
        }
    }
}

}  // namespace

bool draw_waiting() {
    return display::ui::draw_centered_text("Waiting", BACKGROUND, FOREGROUND);
}

bool draw_receiving(const std::uint32_t data_frames) {
    display::clear(BACKGROUND);
    char count[16];
    std::snprintf(count, sizeof(count), "%lu", static_cast<unsigned long>(data_frames));
    return draw_centered_line("Receiving", 100) && draw_centered_line(count, 124);
}

void draw_preview(const Receiver& receiver) {
    draw_image(receiver);
}

}  // namespace gbp::terminal
