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
    using Config::PrinterType;
    const auto width = [](const PrinterType type) { return static_cast<uint32>(type); };
    assert(stepped_setting_value(Setting::PrinterType, width(PrinterType::Mm37), -1) == width(PrinterType::Mm37));
    assert(stepped_setting_value(Setting::PrinterType, width(PrinterType::Mm37), 1) == width(PrinterType::Mm58));
    assert(stepped_setting_value(Setting::PrinterType, width(PrinterType::Mm80), 1) == width(PrinterType::Mm110));
    assert(stepped_setting_value(Setting::PrinterType, width(PrinterType::Mm110), 1) == width(PrinterType::Mm110));
    assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_1X, 1) == Config::SCALE_1_5X);
    assert(stepped_setting_value(Setting::PaperType, 0, 1) == 1);
    assert(stepped_setting_value(Setting::LeftMargin, 0, -1) == 0);
    assert(stepped_setting_value(Setting::LeftMargin, Config::MAX_LEFT_MARGIN_DOTS, 1) ==
           Config::MAX_LEFT_MARGIN_DOTS);
    assert(stepped_setting_value(Setting::LabelLength, 50, 1) == 51);
    assert(stepped_setting_value(Setting::Protocol, 0, 1) ==
           static_cast<uint32>(Config::PrintProtocol::Tspl));
    assert(stepped_setting_value(Setting::LabelGap, 0, 1) == 1);
    assert(!set_setting_value(Setting::LabelGap, Config::MAX_LABEL_GAP_MM + 1, Config::WriteMode::RuntimeOnly));
    assert(stepped_setting_value(Setting::PrintDirection, 1, 1) == 0);
    assert(stepped_setting_value(Setting::PrintDirection, 0, -1) == 1);
    assert(setting_value(Setting::Protocol) == static_cast<uint32>(Config::PrintProtocol::EscPos));
    assert(set_setting_value(Setting::LabelGap, Config::MAX_LABEL_GAP_MM, Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::Protocol, static_cast<uint32>(Config::PrintProtocol::Tspl),
                             Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::Protocol) == static_cast<uint32>(Config::PrintProtocol::Tspl));
    assert(Config::Vars::current_m3_config().protocol == Config::PrintProtocol::EscPos);
    assert(set_setting_value(Setting::PrintDirection, 0, Config::WriteMode::RuntimeOnly));
    assert(Config::Vars::print_direction.get() == 0);
    assert(set_setting_value(Setting::PrintDirection, 1, Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::Protocol, static_cast<uint32>(Config::PrintProtocol::EscPos),
                             Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::LabelGap, 0, Config::WriteMode::RuntimeOnly));

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
    assert(set_setting_value(Setting::PrinterType, width(PrinterType::Mm80), Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::PaperType, 1, Config::WriteMode::RuntimeOnly));
    assert(!set_setting_value(Setting::LeftMargin, 10, Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::Centering, 1, Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::LeftMargin, 10, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::LeftMargin) == 10);
    assert(!set_setting_value(Setting::LeftMargin, Config::MAX_LEFT_MARGIN_DOTS + 1,
                              Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::LabelLength, 75, Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::LabelGap, 2, Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::Protocol, static_cast<uint32>(Config::PrintProtocol::Tspl),
                             Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::PrintDirection) == 1);
    assert(set_setting_value(Setting::PrintDirection, 0, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::PrintDirection) == 0);
    assert(!set_setting_value(Setting::PrintDirection, 2, Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::Protocol) == static_cast<uint32>(Config::PrintProtocol::Tspl));
    assert(stepped_setting_value(Setting::LabelGap, Config::MAX_TSPL_LABEL_GAP_MM, 1) ==
           Config::MAX_TSPL_LABEL_GAP_MM);
    assert(!set_setting_value(Setting::LabelGap, Config::MAX_TSPL_LABEL_GAP_MM + 1,
                              Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::Protocol, static_cast<uint32>(Config::PrintProtocol::EscPos),
                             Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::LabelGap, Config::MAX_LABEL_GAP_MM, Config::WriteMode::RuntimeOnly));
    assert(!set_setting_value(Setting::Protocol, static_cast<uint32>(Config::PrintProtocol::Tspl),
                              Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::LabelGap, Config::MAX_TSPL_LABEL_GAP_MM,
                             Config::WriteMode::RuntimeOnly));
    assert(set_setting_value(Setting::Protocol, static_cast<uint32>(Config::PrintProtocol::Tspl),
                             Config::WriteMode::RuntimeOnly));
    assert(setting_value(Setting::Locale) == 0);
    assert(setting_value(Setting::PrinterType) == width(PrinterType::Mm80));
    assert(setting_value(Setting::PaperType) == 1);
    assert(setting_value(Setting::LabelLength) == 75);
    assert(!set_setting_value(Setting::Backlight, 25, Config::WriteMode::RuntimeAndToml));
    assert(setting_value(Setting::Backlight) == 100);

    constexpr auto runtime = Config::WriteMode::RuntimeOnly;
    struct ScaleLimit { PrinterType type; uint32 max; };
    constexpr ScaleLimit limits[]{
        {PrinterType::Mm110, Config::SCALE_5X}, {PrinterType::Mm80, Config::SCALE_3X},
    };
    for (const auto& limit : limits) {
        assert(set_setting_value(Setting::PrinterType, width(limit.type), runtime));
        assert(setting_value(Setting::ImageScale) == limit.max);
        assert(stepped_setting_value(Setting::ImageScale, limit.max, 1) == limit.max);
        assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_1X, -1) == Config::SCALE_1X);
        assert(!set_setting_value(Setting::ImageScale, limit.max + Config::SCALE_UNIT, runtime));
        assert(!set_setting_value(Setting::ImageScale, Config::SCALE_1_5X, runtime));
        for (uint32 scale = Config::SCALE_1X; scale <= limit.max; scale += Config::SCALE_UNIT) {
            assert(set_setting_value(Setting::ImageScale, scale, runtime));
            assert(setting_value(Setting::ImageScale) == scale);
        }
    }

    assert(set_setting_value(Setting::PrinterType, width(PrinterType::Mm58), runtime));
    assert(setting_value(Setting::ImageScale) == Config::SCALE_2X);
    assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_1X, 1) == Config::SCALE_1_5X);
    assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_1_5X, 1) == Config::SCALE_2X);
    assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_2X, -1) == Config::SCALE_1_5X);
    assert(set_setting_value(Setting::ImageScale, Config::SCALE_1_5X, runtime));
    assert(setting_value(Setting::ImageScale) == Config::SCALE_1_5X);
    assert(setting_value(Setting::LeftMargin) == 72);
    assert(set_setting_value(Setting::ImageScale, Config::SCALE_1X, runtime));
    assert(set_setting_value(Setting::ImageScale, Config::SCALE_2X, runtime));

    assert(set_setting_value(Setting::PrinterType, width(PrinterType::Mm37), runtime));
    assert(setting_value(Setting::ImageScale) == Config::SCALE_1X);
    assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_1X, 1) == Config::SCALE_1_5X);
    assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_1_5X, -1) == Config::SCALE_1X);
    assert(stepped_setting_value(Setting::ImageScale, Config::SCALE_1_5X, 1) == Config::SCALE_1_5X);
    assert(set_setting_value(Setting::ImageScale, Config::SCALE_1_5X, runtime));
    assert(setting_value(Setting::ImageScale) == Config::SCALE_1_5X);
    assert(!set_setting_value(Setting::ImageScale, Config::SCALE_2X, runtime));
    assert(setting_value(Setting::LeftMargin) == 20);

    assert(set_setting_value(Setting::PrinterType, width(PrinterType::Mm110), runtime));
    assert(set_setting_value(Setting::ImageScale, Config::SCALE_4X, runtime));
    assert(set_setting_value(Setting::PrinterType, width(PrinterType::Mm80), runtime));
    assert(setting_value(Setting::ImageScale) == Config::SCALE_3X);
    assert(set_setting_value(Setting::PrinterType, width(PrinterType::Mm110), runtime));
    assert(setting_value(Setting::ImageScale) == Config::SCALE_4X);
    assert(set_setting_value(Setting::Centering, 0, runtime));
    assert(!set_setting_value(Setting::LeftMargin, 15, runtime));
}
