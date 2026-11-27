// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/status_bar.h"

#include <cstdio>
#include <string>
#include <string_view>

#include "btprinter/bluetooth_printer.h"
#include "display/ui/text.h"
#include "display/zjy154s0800.h"
#include "power/battery_monitor.h"
#include "resource/icon/generated_icons.h"

namespace App::StatusBar {
namespace {

constexpr Display::Gray4 BACKGROUND = 0x0F;
constexpr Display::Gray4 FOREGROUND = 0x00;
constexpr int16 MARGIN       = 4;
constexpr int16 TOP_ROW      = 2;
constexpr uint16 TOP_HEIGHT     = 20;

constexpr auto& CHARGING_ICON = Kuper::Icons::ICON_CHARGING_MASK[0];
static_assert(CHARGING_ICON.width == 16 && CHARGING_ICON.height == 16);

void draw_charging_icon(const uint16 x, const uint16 y) {
    for (uint16 row = 0; row < CHARGING_ICON.height; ++row) {
        for (uint16 column = 0; column < CHARGING_ICON.width; ++column) {
            const auto byte = CHARGING_ICON.bytes[row * CHARGING_ICON.row_stride + column / 8];
            if (byte & (0x80u >> (column % 8))) {
                Display::set_pixel(x + column, y + row, FOREGROUND);
            }
        }
    }
}

bool draw_right_aligned(const std::string_view text, const int16 y, const bool charging = false) {
    const int16 width = Display::Ui::measure_utf8(text);
    if (width < 0) {
        return false;
    }
    const int16 x = Display::WIDTH - MARGIN - width;
    if (charging) {
        draw_charging_icon(x - MARGIN - CHARGING_ICON.width, y);
    }
    return Display::Ui::draw_utf8(text, x, y, FOREGROUND);
}

std::string_view bluetooth_text(const BtPrinter::ConnectionState connection) {
    using State = BtPrinter::ConnectionState;
    switch (connection) {
        case State::Disconnected: return "Disconnected";
        case State::Connecting: return "Connecting";
        case State::Connected: return "Connected";
        case State::Error: return "Pair failed";
    }
    return "Disconnected";
}

bool draw_bluetooth(const BtPrinter::ConnectionState connection) {
    std::string name = Display::Ui::translate("Bluetooth");
    const std::string status = Display::Ui::translate(bluetooth_text(connection));
    name.push_back(' ');
    name += status;
    return Display::Ui::draw_utf8(name, MARGIN, TOP_ROW, FOREGROUND);
}

bool draw_battery(const Power::BatteryStatus& battery) {
    using Indication = Power::BatteryStatus::Indication;
    switch (battery.indication) {
        case Indication::External: return draw_right_aligned("EXT", TOP_ROW, battery.charging);
        case Indication::Low: return draw_right_aligned(u8"低电量", TOP_ROW);
        case Indication::Blank: return true;
        case Indication::Percent: {
            char text[5];
            std::snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(battery.percent));
            return draw_right_aligned(text, TOP_ROW, battery.charging);
        }
    }
    return false;
}

}  // namespace

bool draw(const BtPrinter::ConnectionState connection, const Power::BatteryStatus& battery) {
    for (uint16 y = 0; y < TOP_HEIGHT; ++y) {
        for (uint16 x = 0; x < Display::WIDTH; ++x) {
            Display::set_pixel(x, y, BACKGROUND);
        }
    }
    return draw_bluetooth(connection) && draw_battery(battery);
}

}  // namespace App::StatusBar
