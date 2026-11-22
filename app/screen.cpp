// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/screen.h"

#include <array>
#include <cstdio>
#include <string>

#include "types.h"
#include "app/settings.h"
#include "config/printer_config.h"
#include "config/variables.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"

namespace App::Screen {
namespace {

constexpr Display::Gray4 WHITE = 0x0F;
constexpr Display::Gray4 BLACK = 0x00;
constexpr Display::Gray4 DIM = 0x08;
constexpr usize VISIBLE_FILES = 7;

bool centered(const std::string_view text, const int16 y) {
    const std::string translated = Display::Ui::translate(text);
    const int16 width = Display::Ui::measure_utf8(translated);
    if (width < 0 || width > Display::WIDTH) {
        return false;
    }
    return Display::Ui::draw_utf8(translated, static_cast<int16>((Display::WIDTH - width) / 2), y, BLACK);
}

bool draw_label(const std::string_view text, const int16 x, const int16 y, const Display::Gray4 shade = BLACK) {
    return Display::Ui::draw_utf8(Display::Ui::translate(text), x, y, shade);
}

bool draw_setting_value(const Setting setting, const uint32 current,
                        const int16 x, const int16 y) {
    char value[40]{};
    switch (setting) {
        case Setting::Locale: {
            constexpr std::string_view locales[]{"locale_en", "locale_zh", "locale_ja"};
            return Display::Ui::draw_key(locales[current], x, y, BLACK);
        }
        case Setting::Palette: {
            constexpr std::string_view palettes[]{"palette_red", "palette_yellow", "palette_green", "palette_cyan",
                                                  "palette_blue", "palette_magenta", "palette_white"};
            return Display::Ui::draw_key(palettes[current], x, y, BLACK);
        }
        case Setting::Backlight:
            std::snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(current));
            break;
        case Setting::AutoCut:
        case Setting::PrintTime: {
            const uint32 duration = current;
            const unsigned long whole = static_cast<unsigned long>(duration / 1'000'000u);
            const unsigned long fraction = static_cast<unsigned long>(duration % 1'000'000u);
            if (fraction == 0) {
                std::snprintf(value, sizeof(value), "%lus", whole);
            } else {
                char decimal[7]{};
                std::snprintf(decimal, sizeof(decimal), "%06lu", fraction);
                int digits = 6;
                while (digits > 0 && decimal[digits - 1] == '0') --digits;
                std::snprintf(value, sizeof(value), "%lu.%.*ss", whole, digits, decimal);
            }
            break;
        }
        case Setting::RxDelay:
            std::snprintf(value, sizeof(value), "%u us", static_cast<unsigned>(current));
            break;
        case Setting::PrinterType:
            return current < Config::PRINTER_TYPE_COUNT &&
                   Display::Ui::draw_utf8(Config::PRINTER_TYPE_NAMES[current], x, y, BLACK);
        case Setting::ImageScale:
            if (current == Config::SCALE_1_5X) {
                std::snprintf(value, sizeof(value), "1.5X");
            } else {
                std::snprintf(value, sizeof(value), "%uX", static_cast<unsigned>(current / Config::SCALE_UNIT));
            }
            break;
        case Setting::Centering:
            return Display::Ui::draw_key(current == static_cast<uint32>(Config::CenteringMode::Firmware) ?
                                            "centering_firmware" : "centering_printer", x, y, BLACK);
        case Setting::LeftMargin:
            std::snprintf(value, sizeof(value), "%u dot", static_cast<unsigned>(current));
            break;
        case Setting::PaperType:
            return Display::Ui::draw_key(current == 1 ? "paper_label" : "paper_roll", x, y, BLACK);
        case Setting::LabelLength:
            std::snprintf(value, sizeof(value), "%u mm", static_cast<unsigned>(current));
            break;
        case Setting::Protocol:
            std::snprintf(value, sizeof(value), "ESC/POS");
            break;
        case Setting::Count:
            return false;
    }
    return Display::Ui::draw_utf8(value, x, y, BLACK);
}

std::string visible_name(const char* source) {
    std::string label;
    for (const char *cursor = source; *cursor != '\0'; ++cursor) {
        const char ch = *cursor;
        const bool supported = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                               (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '_' || ch == '-';
        label.push_back(supported ? ch : '?');
        if (Display::Ui::measure_utf8(label) > Display::WIDTH - 42) {
            label.pop_back();
            break;
        }
    }
    return label;
}

}  // namespace

bool draw_menu(const uint8 selection) {
    Display::clear(WHITE);
    constexpr std::string_view keys[]{"mode_m1", "mode_m2", "mode_m3", "mode_m13", "settings"};
    if (!centered("menu", 14)) return false;
    for (uint8 row = 0; row < 5; ++row) {
        const int16 y = static_cast<int16>(49 + row * 37);
        if (!Display::Ui::draw_utf8(selection == row ? ">" : " ", 38, y, BLACK) ||
            !Display::Ui::draw_key(keys[row], 66, y, BLACK)) return false;
    }
    return true;
}

bool draw_settings(const Setting selection, const bool editing,
                   const std::string_view error) {
    constexpr std::string_view labels[]{"setting_locale", "setting_palette", "setting_backlight",
                                        "setting_auto_cut", "setting_print_time", "setting_rx_delay",
                                        "setting_printer_type", "setting_image_scale", "setting_centering",
                                        "setting_left_margin", "setting_paper_type", "setting_label_length",
                                        "setting_protocol"};
    static_assert(sizeof(labels) / sizeof(*labels) == static_cast<usize>(Setting::Count));
    Display::clear(WHITE);
    if (!centered("settings", 12)) return false;
    constexpr uint8 visible_rows = 7;
    const bool firmware_centering = Config::Vars::centering.get() == Config::CenteringMode::Firmware;
    std::array<Setting, static_cast<usize>(Setting::Count)> visible{};
    uint8 count = 0;
    uint8 selected = 0;
    for (uint8 index = 0; index < static_cast<uint8>(Setting::Count); ++index) {
        const auto item = static_cast<Setting>(index);
        if (!setting_visible(item, firmware_centering)) continue;
        if (item == selection) selected = count;
        visible[count++] = item;
    }
    // Show the effective scale alongside the paper width while it is being changed.
    const uint8 last = selection == Setting::PrinterType ? selected + 1 : selected;
    const uint8 first = last < visible_rows ? 0 : last - visible_rows + 1;
    for (uint8 row = 0; row < visible_rows && first + row < count; ++row) {
        const auto item = visible[first + row];
        const int16 y = static_cast<int16>(38 + row * 24);
        if (!Display::Ui::draw_utf8(selection == item ? ">" : " ", 4, y, BLACK) ||
            !Display::Ui::draw_key(labels[static_cast<uint8>(item)], 25, y, BLACK) ||
            !draw_setting_value(item, setting_value(item), 155, y)) return false;
    }
    if (editing) {
        const std::string_view hint = selection == Setting::LeftMargin ? "settings_margin_hint" :
            "settings_adjust_hint";
        return centered(error.empty() ? hint : error, 204) &&
               centered("settings_save_hint", 221);
    }
    return centered("settings_edit_hint", 218);
}

bool draw_m3_menu(const uint8 selection, const bool connected, const bool m13) {
    Display::clear(WHITE);
    return centered(m13 ? "M1+3" : "M3", 35) &&
           Display::Ui::draw_utf8(selection == 0 ? ">" : " ", 52, 94, BLACK) &&
           draw_label("Pair", 80, 94) &&
           Display::Ui::draw_utf8(selection == 1 ? ">" : " ", 52, 137, BLACK) &&
           draw_label("Print", 80, 137, connected ? BLACK : DIM) &&
           (connected || centered("Pair first", 190));
}

bool draw_m13_receive(const bool countdown, const uint32 remaining_us,
                      const bool awaiting_cut, const bool saving, const bool printing,
                      const std::string_view error) {
    Display::clear(WHITE);
    if (!error.empty()) {
        return centered(error, 108);
    }
    if (printing) {
        return centered("BT Printing", 108);
    }
    if (saving) {
        return centered("Saving GBR", 108);
    }
    if (countdown) {
        char seconds[24]{};
        const unsigned long whole = static_cast<unsigned long>(remaining_us / 1'000'000u);
        const unsigned long tenths = static_cast<unsigned long>((remaining_us % 1'000'000u + 99'999u) / 100'000u);
        std::snprintf(seconds, sizeof(seconds), "%lu.%lus", whole + tenths / 10u, tenths % 10u);
        return centered("AutoCut", 84) && centered(seconds, 126);
    }
    if (awaiting_cut) {
        return centered("AutoCut", 84) && centered("Waiting", 126);
    }
    return centered("Waiting", 108) && centered("Left back", 207);
}

bool draw_m3_pairing(const uint8 selection, const std::string_view status) {
    Display::clear(WHITE);
    return centered("Bluetooth", 33) && centered(status, 72) &&
           Display::Ui::draw_utf8(selection == 0 ? ">" : " ", 40, 116, BLACK) &&
           draw_label("Re-pair", 68, 116) &&
           Display::Ui::draw_utf8(selection == 1 ? ">" : " ", 40, 155, BLACK) &&
           draw_label("Disconnect", 68, 155) &&
           centered("Left back", 207);
}

bool draw_paired_badge() {
    return draw_label("PAIRED", 4, 2);
}

bool draw_file_list(const Storage::GbrCatalog& catalog, const usize selection, const bool bluetooth) {
    Display::clear(WHITE);
    if (!centered(bluetooth ? "BT files" : "GBR files", 12)) {
        return false;
    }
    if (catalog.count() == 0) {
        return centered("No GBR files", 108);
    }

    const usize first = (selection / VISIBLE_FILES) * VISIBLE_FILES;
    char name[Storage::GBR_NAME_CAPACITY]{};
    for (usize row = 0; row < VISIBLE_FILES && first + row < catalog.count(); ++row) {
        if (!catalog.name_at(first + row, name, sizeof(name))) {
            return false;
        }
        const int16 y = static_cast<int16>(39 + row * 26);
        if (first + row == selection && !Display::Ui::draw_utf8(">", 8, y, BLACK)) {
            return false;
        }
        if (!Display::Ui::draw_utf8(visible_name(name), 32, y, BLACK)) {
            return false;
        }
    }
    char page[24]{};
    std::snprintf(page, sizeof(page), "%lu/%lu", static_cast<unsigned long>(selection + 1),
                  static_cast<unsigned long>(catalog.count()));
    return centered(page, 217);
}

bool draw_printing(const usize sent, const usize total, const bool can_cancel) {
    Display::clear(WHITE);
    char progress[16]{};
    std::snprintf(progress, sizeof(progress), "%lu%%",
                  static_cast<unsigned long>(total == 0 ? 0 : sent * 100 / total));
    return centered("Printing", 97) && centered(progress, 130) &&
           (!can_cancel || centered("Left cancel", 177));
}

bool draw_label_warning(const usize content_dots, const usize label_dots, const bool interactive) {
    Display::clear(WHITE);
    char dimensions[32]{};
    const unsigned long content_mm = static_cast<unsigned long>((content_dots * 254 + 2029) / 2030);
    const unsigned long label_mm = static_cast<unsigned long>((label_dots * 254 + 1015) / 2030);
    std::snprintf(dimensions, sizeof(dimensions), "%lu > %lu mm", content_mm, label_mm);
    return centered("Label too long", 73) && centered(dimensions, 109) &&
           (!interactive || (centered("OK continue", 160) && centered("Left cancel", 191)));
}

bool draw_block_indicator(const usize index, const usize count) {
    if (count <= 1) {
        return true;
    }
    char label[24]{};
    std::snprintf(label, sizeof(label), "%lu/%lu", static_cast<unsigned long>(index + 1),
                  static_cast<unsigned long>(count));
    for (uint16 y = 0; y < 20; ++y) {
        for (uint16 x = 0; x < Display::WIDTH; ++x) {
            Display::set_pixel(x, y, WHITE);
        }
    }
    return centered(label, 2);
}

bool draw_error(const std::string_view message) {
    Display::clear(WHITE);
    return centered(message, 108);
}

}  // namespace App::Screen
