// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <cassert>
#include <string_view>

#include "app/settings.h"
#include "config/variables.h"

namespace Storage {
bool read_config_text(std::string_view&) { return false; }
bool write_config_value(std::string_view, std::string_view) { return false; }
}  // namespace Storage

int main() {
    using namespace App;
    Config::Controller::instance().reset_defaults();

    assert(stepped_setting_value(Setting::Locale, 0, -1) == 2);
    assert(stepped_setting_value(Setting::Locale, 2, 1) == 0);
    assert(stepped_setting_value(Setting::Palette, 0, -1) == Display::PALETTE_COUNT - 1);
    assert(stepped_setting_value(Setting::Palette, Display::PALETTE_COUNT - 1, 1) == 0);
    assert(stepped_setting_value(Setting::Backlight, 0, -1) == 0);
    assert(stepped_setting_value(Setting::Backlight, 99, 1) == 100);
    assert(stepped_setting_value(Setting::AutoCut, 0, -1) == 0);
    assert(stepped_setting_value(Setting::AutoCut, 60'000'000, 1) == 60'000'000);
    assert(stepped_setting_value(Setting::PrintTime, 500'000, -1) == 0);
    assert(stepped_setting_value(Setting::PrintTime, 59'500'000, 1) == 60'000'000);
    assert(stepped_setting_value(Setting::RxDelay, 2, -1) == 2);
    assert(stepped_setting_value(Setting::RxDelay, 10, 1) == 10);
    assert(stepped_setting_value(Setting::PrinterType, 0, 1) == 1);
    assert(stepped_setting_value(Setting::PrinterType, 1, -1) == 0);

    assert(set_setting_value(Setting::Backlight, 5, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::Backlight) == 5);
    assert(set_setting_value(Setting::Backlight, 300, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::Backlight) == 100);
    assert(set_setting_value(Setting::RxDelay, 0, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::RxDelay) == 2);
    assert(set_setting_value(Setting::Palette, 300, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::Palette) == Display::PALETTE_COUNT - 1);
    assert(!set_setting_value(Setting::Locale, 256, Config::WriteMode::RuntimeOnly));
    assert(!set_setting_value(Setting::PrinterType, 256, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::Locale) == 0);
    assert(setting_value(Setting::PrinterType) == 0);
    assert(!set_setting_value(Setting::Backlight, 25, Config::WriteMode::RuntimeAndToml));
    assert(setting_value(Setting::Backlight) == 100);
}
