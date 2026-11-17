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
    return centered("Menu", 62) &&
           display::ui::draw_utf8(selection == 0 ? ">" : " ", 74, 105, BLACK) &&
           display::ui::draw_utf8("M1", 100, 105, BLACK) &&
           display::ui::draw_utf8(selection == 1 ? ">" : " ", 74, 140, BLACK) &&
           display::ui::draw_utf8("M2", 100, 140, BLACK);
}

bool draw_file_list(const storage::GbrCatalog& catalog, const std::size_t selection) {
    display::clear(WHITE);
    if (!centered("GBR files", 12)) return false;
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

bool draw_printing(const std::size_t sent, const std::size_t total) {
    display::clear(WHITE);
    char progress[16]{};
    std::snprintf(progress, sizeof(progress), "%lu%%",
                  static_cast<unsigned long>(total == 0 ? 0 : sent * 100 / total));
    return centered("Printing", 97) && centered(progress, 130);
}

bool draw_error(const std::string_view message) {
    display::clear(WHITE);
    return centered(message, 108);
}

}  // namespace app::screen
