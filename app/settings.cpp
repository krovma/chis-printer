// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "app/settings.h"

#include <algorithm>
#include <limits>

#include "types.h"
#include "config/variables.h"
#include "display/zjy154s0800.h"

namespace App {
namespace {

uint32 bounded(const int64 value, const uint32 minimum, const uint32 maximum) {
    return static_cast<uint32>(value < minimum ? minimum : value > maximum ? maximum : value);
}

}  // namespace

uint32 setting_value(const Setting setting) {
    switch (setting) {
        case Setting::Locale: return static_cast<uint32>(Config::Vars::locale.get());
        case Setting::Palette: return Config::Vars::palette.get();
        case Setting::Backlight: return Config::Vars::backlight_percent.get();
        case Setting::AutoCut: return Config::Vars::auto_cut_time_limit_us.get();
        case Setting::PrintTime: return Config::Vars::simulated_print_time_us.get();
        case Setting::RxDelay: return Config::Vars::rx_sample_delay_us.get();
        case Setting::PrinterType: return static_cast<uint32>(Config::Vars::printer_type.get());
        case Setting::Count: return 0;
    }
    return 0;
}

uint32 stepped_setting_value(const Setting setting, const uint32 current, const int direction) {
    if (direction != -1 && direction != 1) return current;
    switch (setting) {
        case Setting::Locale: return static_cast<uint32>((static_cast<int>(current) + direction + 3) % 3);
        case Setting::Palette:
            return static_cast<uint32>((static_cast<int>(current) + direction +
                                        static_cast<int>(Display::PALETTE_COUNT)) %
                                       static_cast<int>(Display::PALETTE_COUNT));
        case Setting::Backlight: return bounded(static_cast<int64>(current) + direction * 5, 0, 100);
        case Setting::AutoCut:
        case Setting::PrintTime:
            return bounded(static_cast<int64>(current) + static_cast<int64>(direction) * 1'000'000,
                           0, 60'000'000);
        case Setting::RxDelay: return bounded(static_cast<int64>(current) + direction, 2, 10);
        case Setting::PrinterType: return direction > 0 ? 1 : 0;
        case Setting::Count: return current;
    }
    return current;
}

bool set_setting_value(const Setting setting, const uint32 value, const Config::WriteMode mode) {
    const auto narrowed = static_cast<uint8>(std::min(value, static_cast<uint32>(std::numeric_limits<uint8>::max())));
    switch (setting) {
        case Setting::Locale:
            return value <= static_cast<uint32>(Display::Ui::Locale::Japanese) &&
                   Config::Vars::locale.set(static_cast<Display::Ui::Locale>(value), mode);
        case Setting::Palette:
            return Config::Vars::palette.set(narrowed, mode);
        case Setting::Backlight:
            return Config::Vars::backlight_percent.set(narrowed, mode);
        case Setting::AutoCut:
            return Config::Vars::auto_cut_time_limit_us.set(value, mode);
        case Setting::PrintTime:
            return Config::Vars::simulated_print_time_us.set(value, mode);
        case Setting::RxDelay:
            return Config::Vars::rx_sample_delay_us.set(narrowed, mode);
        case Setting::PrinterType:
            return value <= static_cast<uint32>(Config::PrinterType::Mm80) &&
                   Config::Vars::printer_type.set(static_cast<Config::PrinterType>(value), mode);
        case Setting::Count:
            return false;
    }
    return false;
}

}  // namespace App
