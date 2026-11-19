// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "config/firmware_config.h"

#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cctype>
#include <string_view>

#include "btprinter/escpos.h"
#include "tomlc17.h"

namespace config {
namespace {

constexpr std::array<std::string_view, display::PALETTE_COUNT> PALETTE_NAMES{
    "red", "yellow", "green", "cyan", "blue", "magenta", "white",
};

template <std::size_t N>
bool copy_text(const toml_datum_t datum, std::array<char, N>& output) {
    if (datum.type != TOML_STRING || datum.u.str.len <= 0 ||
        static_cast<std::size_t>(datum.u.str.len) >= output.size()) return false;
    const std::size_t length = static_cast<std::size_t>(datum.u.str.len);
    for (std::size_t index = 0; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(datum.u.str.ptr[index]);
        if (value < 0x20 || value == 0x7f) return false;
        output[index] = static_cast<char>(value);
    }
    output[length] = '\0';
    return true;
}

void parse_m3_advance(const toml_datum_t printable_width, const toml_datum_t image_scale,
                      const PrinterType type, const char* width_key, const char* scale_key,
                      M3AdvanceConfig& output) {
    const M3AdvanceConfig defaults = default_m3_advance(type);
    const std::int64_t minimum_width = static_cast<std::int64_t>(btprinter::SOURCE_WIDTH * defaults.image_scale);
    if (printable_width.type == TOML_INT64 && printable_width.u.int64 >= minimum_width &&
        printable_width.u.int64 <= 1023) {
        output.printable_width_dots = static_cast<std::uint16_t>(printable_width.u.int64);
    } else if (printable_width.type != TOML_UNKNOWN) {
        std::printf("config.toml m3.advance.%s invalid; using %u\n", width_key,
                    static_cast<unsigned>(defaults.printable_width_dots));
    }
    if (image_scale.type == TOML_INT64 && image_scale.u.int64 == defaults.image_scale) {
        output.image_scale = static_cast<std::uint8_t>(image_scale.u.int64);
    } else if (image_scale.type != TOML_UNKNOWN) {
        std::printf("config.toml m3.advance.%s conflicts with printer_type; using %ux\n", scale_key,
                    static_cast<unsigned>(defaults.image_scale));
    }
}

}  // namespace

bool parse_firmware_config(const std::string_view contents, FirmwareConfig& output) {
    if (contents.size() > INT_MAX) {
        return false;
    }

    const toml_result_t document = toml_parse(contents.data(), static_cast<int>(contents.size()));
    if (!document.ok) {
        toml_free(document);
        return false;
    }

    const toml_datum_t version = toml_get(document.toptab, "format_version");
    const toml_datum_t palette = toml_seek(document.toptab, "display.palette");
    const toml_datum_t backlight = toml_seek(document.toptab, "display.backlight_percent");
    const toml_datum_t lower_m1 = toml_get(document.toptab, "m1");
    const bool legacy_m1 = lower_m1.type == TOML_UNKNOWN;
    const toml_datum_t m1 = legacy_m1 ? toml_get(document.toptab, "M1") : lower_m1;
    const toml_datum_t auto_cut = toml_seek(document.toptab, legacy_m1 ?
        "M1.AutoCutTimeLimit" : "m1.auto_cut_time_limit");
    const toml_datum_t print_time = toml_seek(document.toptab, legacy_m1 ?
        "M1.SimulatedPrintTime" : "m1.simulated_print_time");
    const toml_datum_t m2 = toml_get(document.toptab, "m2");
    const toml_datum_t rx_delay = toml_seek(document.toptab, "m2.rx_sample_delay_us");
    const toml_datum_t m3 = toml_get(document.toptab, "m3");
    const toml_datum_t printer_name = toml_seek(document.toptab, "m3.printer_name");
    const toml_datum_t pin = toml_seek(document.toptab, "m3.pin");
    const toml_datum_t printer_type = toml_seek(document.toptab, "m3.printer_type");
    const toml_datum_t advance = toml_seek(document.toptab, "m3.advance");
    const toml_datum_t width_58mm = toml_seek(document.toptab, "m3.advance.printable_width_dots_58mm");
    const toml_datum_t scale_58mm = toml_seek(document.toptab, "m3.advance.image_scale_58mm");
    const toml_datum_t width_80mm = toml_seek(document.toptab, "m3.advance.printable_width_dots_80mm");
    const toml_datum_t scale_80mm = toml_seek(document.toptab, "m3.advance.image_scale_80mm");

    bool valid = version.type == TOML_INT64 && version.u.int64 == 1 &&
                 palette.type == TOML_STRING && backlight.type == TOML_INT64 &&
                 backlight.u.int64 >= 0 && backlight.u.int64 <= 100;
    FirmwareConfig parsed;
    if (valid) {
        const std::string_view palette_name{palette.u.str.ptr, static_cast<std::size_t>(palette.u.str.len)};
        valid = false;
        for (std::size_t index = 0; index < PALETTE_NAMES.size(); ++index) {
            if (palette_name == PALETTE_NAMES[index]) {
                parsed.display.palette_key = static_cast<std::uint8_t>(index);
                parsed.display.backlight_percent = static_cast<std::uint8_t>(backlight.u.int64);
                valid = true;
                break;
            }
        }
    }
    if (valid && m2.type != TOML_UNKNOWN) {
        if (m2.type == TOML_TABLE && rx_delay.type == TOML_INT64 &&
            gbp::host::valid_rx_sample_delay_us(rx_delay.u.int64)) {
            parsed.m2.rx_sample_delay_us = static_cast<std::uint8_t>(rx_delay.u.int64);
        } else if (m2.type != TOML_TABLE || rx_delay.type != TOML_UNKNOWN) {
            std::printf("config.toml m2.rx_sample_delay_us invalid; using %u us (allowed %u-%u)\n",
                        static_cast<unsigned>(gbp::host::RX_SAMPLE_DELAY_DEFAULT_US),
                        static_cast<unsigned>(gbp::host::RX_SAMPLE_DELAY_MIN_US),
                        static_cast<unsigned>(gbp::host::RX_SAMPLE_DELAY_MAX_US));
        }
    }
    if (valid && m1.type != TOML_UNKNOWN) {
        const bool numeric = auto_cut.type == TOML_INT64 || auto_cut.type == TOML_FP64;
        const double seconds = auto_cut.type == TOML_INT64 ? static_cast<double>(auto_cut.u.int64) :
                               auto_cut.type == TOML_FP64 ? auto_cut.u.fp64 : 0.0;
        if (m1.type == TOML_TABLE && numeric && std::isfinite(seconds) &&
            seconds >= 0.0 && seconds <= 60.0) {
            parsed.m1.auto_cut_time_limit_us = static_cast<std::uint32_t>(seconds * 1'000'000.0 + 0.5);
        } else if (m1.type != TOML_TABLE || auto_cut.type != TOML_UNKNOWN) {
            std::printf("config.toml m1.auto_cut_time_limit invalid; using 1 s (allowed 0-60 s)\n");
        }
        const bool print_numeric = print_time.type == TOML_INT64 || print_time.type == TOML_FP64;
        const double print_seconds = print_time.type == TOML_INT64 ? static_cast<double>(print_time.u.int64) :
                                     print_time.type == TOML_FP64 ? print_time.u.fp64 : 0.0;
        if (m1.type == TOML_TABLE && print_numeric && std::isfinite(print_seconds) &&
            print_seconds >= 0.0 && print_seconds <= 60.0) {
            parsed.m1.simulated_print_time_us = static_cast<std::uint32_t>(print_seconds * 1'000'000.0 + 0.5);
        } else if (m1.type != TOML_TABLE || print_time.type != TOML_UNKNOWN) {
            std::printf("config.toml m1.simulated_print_time invalid; using 1 s (allowed 0-60 s)\n");
        }
    }

    if (valid && m3.type != TOML_UNKNOWN) {
        M3Config parsed_m3{};
        const bool m3_valid = m3.type == TOML_TABLE && copy_text(printer_name, parsed_m3.printer_name) &&
                              copy_text(pin, parsed_m3.pin);
        if (m3_valid) {
            if (printer_type.type == TOML_STRING) {
                const std::string_view name{printer_type.u.str.ptr,
                                            static_cast<std::size_t>(printer_type.u.str.len)};
                if (name == "58mm") parsed_m3.printer_type = PrinterType::Mm58;
                else if (name == "80mm") parsed_m3.printer_type = PrinterType::Mm80;
                else std::printf("config.toml m3.printer_type invalid; using 58mm\n");
            } else if (printer_type.type != TOML_UNKNOWN) {
                std::printf("config.toml m3.printer_type invalid; using 58mm\n");
            }
            if (advance.type == TOML_TABLE) {
                parse_m3_advance(width_58mm, scale_58mm, PrinterType::Mm58,
                                 "printable_width_dots_58mm", "image_scale_58mm", parsed_m3.advance.mm58);
                parse_m3_advance(width_80mm, scale_80mm, PrinterType::Mm80,
                                 "printable_width_dots_80mm", "image_scale_80mm", parsed_m3.advance.mm80);
            } else if (advance.type != TOML_UNKNOWN) {
                std::printf("config.toml m3.advance invalid; using printer_type defaults\n");
            }
            parsed.m3 = parsed_m3;
        } else {
            std::printf("config.toml m3 printer settings invalid; using defaults\n");
        }
    }

    toml_free(document);
    if (valid) {
        output = parsed;
    }
    return valid;
}

}  // namespace config
