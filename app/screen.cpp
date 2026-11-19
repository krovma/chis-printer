// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/screen.h"

#include <cstdio>
#include <string>

#include "display/ui/text.h"
#include "display/zjy154s0800.h"

namespace app::screen {
namespace {

constexpr display::Gray4 WHITE = 0x0F;
constexpr display::Gray4 BLACK = 0x00;
constexpr display::Gray4 DIM = 0x08;
constexpr std::size_t VISIBLE_FILES = 7;

bool centered(const std::string_view text, const std::int16_t y) {
    const std::int16_t width = display::ui::measure_utf8(text);
    if (width < 0 || width > display::WIDTH) return false;
    return display::ui::draw_utf8(text, static_cast<std::int16_t>((display::WIDTH - width) / 2), y, BLACK);
}

std::string visible_name(const char* source) {
    std::string label;
    for (const char* cursor = source; *cursor != '\0'; ++cursor) {
        const char ch = *cursor;
        const bool supported = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                               (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '_' || ch == '-';
        label.push_back(supported ? ch : '?');
        if (display::ui::measure_utf8(label) > display::WIDTH - 42) {
            label.pop_back();
            break;
        }
    }
    return label;
}

}  // namespace

bool draw_menu(const std::uint8_t selection) {
    display::clear(WHITE);
    return centered("Menu", 35) &&
           display::ui::draw_utf8(selection == 0 ? ">" : " ", 74, 68, BLACK) &&
           display::ui::draw_utf8("M1", 100, 68, BLACK) &&
           display::ui::draw_utf8(selection == 1 ? ">" : " ", 74, 103, BLACK) &&
           display::ui::draw_utf8("M2", 100, 103, BLACK) &&
           display::ui::draw_utf8(selection == 2 ? ">" : " ", 74, 138, BLACK) &&
           display::ui::draw_utf8("M3", 100, 138, BLACK) &&
           display::ui::draw_utf8(selection == 3 ? ">" : " ", 74, 173, BLACK) &&
           display::ui::draw_utf8("M1+3", 100, 173, BLACK);
}

bool draw_m3_menu(const std::uint8_t selection, const bool connected, const bool m13) {
    display::clear(WHITE);
    return centered(m13 ? "M1+3" : "M3", 35) &&
           display::ui::draw_utf8(selection == 0 ? ">" : " ", 52, 94, BLACK) &&
           display::ui::draw_utf8("Pair", 80, 94, BLACK) &&
           display::ui::draw_utf8(selection == 1 ? ">" : " ", 52, 137, BLACK) &&
           display::ui::draw_utf8("Print", 80, 137, connected ? BLACK : DIM) &&
           (connected || centered("Pair first", 190));
}

bool draw_m13_receive(const bool countdown, const std::uint32_t remaining_us,
                      const bool awaiting_cut, const bool saving, const bool printing,
                      const std::string_view error) {
    display::clear(WHITE);
    if (!error.empty()) return centered(error, 108);
    if (printing) return centered("BT Printing", 108);
    if (saving) return centered("Saving GBR", 108);
    if (countdown) {
        char seconds[24]{};
        const unsigned long whole = static_cast<unsigned long>(remaining_us / 1'000'000u);
        const unsigned long tenths = static_cast<unsigned long>((remaining_us % 1'000'000u + 99'999u) / 100'000u);
        std::snprintf(seconds, sizeof(seconds), "%lu.%lus", whole + tenths / 10u, tenths % 10u);
        return centered("AutoCut", 84) && centered(seconds, 126);
    }
    if (awaiting_cut) return centered("AutoCut", 84) && centered("Waiting", 126);
    return centered("Waiting", 108) && centered("Left back", 207);
}

bool draw_m3_pairing(const std::uint8_t selection, const std::string_view status) {
    display::clear(WHITE);
    return centered("Bluetooth", 33) && centered(status, 72) &&
           display::ui::draw_utf8(selection == 0 ? ">" : " ", 40, 116, BLACK) &&
           display::ui::draw_utf8("Re-pair", 68, 116, BLACK) &&
           display::ui::draw_utf8(selection == 1 ? ">" : " ", 40, 155, BLACK) &&
           display::ui::draw_utf8("Disconnect", 68, 155, BLACK) &&
           centered("Left back", 207);
}

bool draw_paired_badge() {
    return display::ui::draw_utf8("PAIRED", 4, 2, BLACK);
}

bool draw_file_list(const storage::GbrCatalog& catalog, const std::size_t selection, const bool bluetooth) {
    display::clear(WHITE);
    if (!centered(bluetooth ? "BT files" : "GBR files", 12)) return false;
    if (catalog.count() == 0) return centered("No GBR files", 108);

    const std::size_t first = (selection / VISIBLE_FILES) * VISIBLE_FILES;
    char name[storage::GBR_NAME_CAPACITY]{};
    for (std::size_t row = 0; row < VISIBLE_FILES && first + row < catalog.count(); ++row) {
        if (!catalog.name_at(first + row, name, sizeof(name))) return false;
        const std::int16_t y = static_cast<std::int16_t>(39 + row * 26);
        if (first + row == selection && !display::ui::draw_utf8(">", 8, y, BLACK)) return false;
        if (!display::ui::draw_utf8(visible_name(name), 32, y, BLACK)) return false;
    }
    char page[24]{};
    std::snprintf(page, sizeof(page), "%lu/%lu", static_cast<unsigned long>(selection + 1),
                  static_cast<unsigned long>(catalog.count()));
    return centered(page, 217);
}

bool draw_printing(const std::size_t sent, const std::size_t total, const bool can_cancel) {
    display::clear(WHITE);
    char progress[16]{};
    std::snprintf(progress, sizeof(progress), "%lu%%",
                  static_cast<unsigned long>(total == 0 ? 0 : sent * 100 / total));
    return centered("Printing", 97) && centered(progress, 130) &&
           (!can_cancel || centered("Left cancel", 177));
}

bool draw_block_indicator(const std::size_t index, const std::size_t count) {
    if (count <= 1) return true;
    char label[24]{};
    std::snprintf(label, sizeof(label), "%lu/%lu", static_cast<unsigned long>(index + 1),
                  static_cast<unsigned long>(count));
    for (std::uint16_t y = 0; y < 20; ++y) {
        for (std::uint16_t x = 0; x < display::WIDTH; ++x) display::set_pixel(x, y, WHITE);
    }
    return centered(label, 2);
}

bool draw_error(const std::string_view message) {
    display::clear(WHITE);
    return centered(message, 108);
}

}  // namespace app::screen
