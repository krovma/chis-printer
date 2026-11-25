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
        case Setting::ImageScale: return Config::Vars::current_m3_config().resolved_advance().image_scale_quarters;
        case Setting::Centering: return static_cast<uint32>(Config::Vars::centering.get());
        case Setting::LeftMargin: return Config::Vars::current_m3_config().resolved_advance().left_margin_dots;
        case Setting::PaperType: return static_cast<uint32>(Config::Vars::paper_type.get());
        case Setting::LabelLength: return Config::Vars::label_length_mm.get();
        case Setting::LabelGap: return Config::Vars::label_gap_mm.get();
        case Setting::Protocol: return static_cast<uint32>(Config::Vars::print_protocol.get());
        case Setting::PrintDirection: return Config::Vars::print_direction.get();
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
        case Setting::PrinterType:
            return bounded(static_cast<int64>(current) + direction, 0, Config::PRINTER_TYPE_COUNT - 1);
        case Setting::ImageScale:
            if (Config::Vars::printer_type.get() == Config::PrinterType::Mm37 ||
                Config::Vars::printer_type.get() == Config::PrinterType::Mm58) {
                if (direction > 0) {
                    if (current < Config::SCALE_1_5X) return Config::SCALE_1_5X;
                    return Config::Vars::printer_type.get() == Config::PrinterType::Mm37 ?
                        Config::SCALE_1_5X : Config::SCALE_2X;
                }
                return current > Config::SCALE_1_5X ? Config::SCALE_1_5X : Config::SCALE_1X;
            }
            return bounded(static_cast<int64>(current) + direction * Config::SCALE_UNIT, Config::SCALE_1X,
                           Config::default_m3_advance(Config::Vars::printer_type.get()).image_scale_quarters);
        case Setting::Centering:
            return static_cast<uint32>(direction > 0 ? Config::CenteringMode::Firmware : Config::CenteringMode::Printer);
        case Setting::LeftMargin:
            return bounded(static_cast<int64>(current) + direction, 0, Config::MAX_LEFT_MARGIN_DOTS);
        case Setting::PaperType: return direction > 0 ? 1 : 0;
        case Setting::LabelLength:
            return bounded(static_cast<int64>(current) + direction, 1, Config::MAX_LABEL_LENGTH_MM);
        case Setting::LabelGap:
            return bounded(static_cast<int64>(current) + direction, 0,
                           Config::Vars::print_protocol.get() == Config::PrintProtocol::Tspl ?
                               Config::MAX_TSPL_LABEL_GAP_MM : Config::MAX_LABEL_GAP_MM);
        case Setting::Protocol:
            return direction > 0 ? static_cast<uint32>(Config::PrintProtocol::Tspl) :
                                   static_cast<uint32>(Config::PrintProtocol::EscPos);
        case Setting::PrintDirection: return current == 0 ? 1 : 0;
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
            return value < Config::PRINTER_TYPE_COUNT &&
                   Config::Vars::printer_type.set(static_cast<Config::PrinterType>(value), mode);
        case Setting::ImageScale:
            return value == narrowed && Config::Vars::set_m3_image_scale(narrowed, mode);
        case Setting::Centering:
            return value <= static_cast<uint32>(Config::CenteringMode::Firmware) &&
                   Config::Vars::centering.set(static_cast<Config::CenteringMode>(value), mode);
        case Setting::LeftMargin:
            return Config::Vars::centering.get() == Config::CenteringMode::Firmware &&
                   value <= Config::MAX_LEFT_MARGIN_DOTS &&
                   Config::Vars::set_m3_left_margin_dots(static_cast<uint16>(value), mode);
        case Setting::PaperType:
            return value <= static_cast<uint32>(Config::PaperType::Label) &&
                   Config::Vars::paper_type.set(static_cast<Config::PaperType>(value), mode);
        case Setting::LabelLength:
            return Config::Vars::label_length_mm.set(
                static_cast<uint16>(std::min<uint32>(value, Config::MAX_LABEL_LENGTH_MM)), mode);
        case Setting::LabelGap: {
            const uint16 max_gap = Config::Vars::print_protocol.get() == Config::PrintProtocol::Tspl ?
                Config::MAX_TSPL_LABEL_GAP_MM : Config::MAX_LABEL_GAP_MM;
            return value <= max_gap && Config::Vars::label_gap_mm.set(static_cast<uint16>(value), mode);
        }
        case Setting::Protocol:
            if (value == static_cast<uint32>(Config::PrintProtocol::EscPos)) {
                return Config::Vars::print_protocol.set(Config::PrintProtocol::EscPos, mode);
            }
            return value == static_cast<uint32>(Config::PrintProtocol::Tspl) &&
                   (Config::Vars::paper_type.get() != Config::PaperType::Label ||
                    Config::Vars::label_gap_mm.get() <= Config::MAX_TSPL_LABEL_GAP_MM) &&
                   Config::Vars::print_protocol.set(Config::PrintProtocol::Tspl, mode);
        case Setting::PrintDirection:
            return value <= 1 &&
                   Config::Vars::print_protocol.get() == Config::PrintProtocol::Tspl &&
                   Config::Vars::print_direction.set(static_cast<uint8>(value), mode);
        case Setting::Count:
            return false;
    }
    return false;
}

}  // namespace App
