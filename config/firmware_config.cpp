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

#include "tomlc17.h"

namespace config {
namespace {

constexpr std::array<std::string_view, display::PALETTE_COUNT> PALETTE_NAMES{
    "red", "yellow", "green", "cyan", "blue", "magenta", "white",
};

int hex_digit(const char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool parse_mac(const toml_datum_t datum, std::array<std::uint8_t, 6>& output) {
    if (datum.type != TOML_STRING || datum.u.str.len != 17) return false;
    const std::string_view text{datum.u.str.ptr, static_cast<std::size_t>(datum.u.str.len)};
    for (std::size_t index = 0; index < output.size(); ++index) {
        const std::size_t offset = index * 3;
        if (index != 0 && text[offset - 1] != ':' && text[offset - 1] != '-') return false;
        const int high = hex_digit(text[offset]);
        const int low = hex_digit(text[offset + 1]);
        if (high < 0 || low < 0) return false;
        output[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

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
    const toml_datum_t m1 = toml_get(document.toptab, "M1");
    const toml_datum_t auto_cut = toml_seek(document.toptab, "M1.AutoCutTimeLimit");
    const toml_datum_t m2 = toml_get(document.toptab, "m2");
    const toml_datum_t rx_delay = toml_seek(document.toptab, "m2.rx_sample_delay_us");
    const toml_datum_t m3 = toml_get(document.toptab, "m3");
    const toml_datum_t printer_name = toml_seek(document.toptab, "m3.printer_name");
    const toml_datum_t pin = toml_seek(document.toptab, "m3.pin");
    const toml_datum_t mac = toml_seek(document.toptab, "m3.mac");

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
            std::printf("config.toml M1.AutoCutTimeLimit invalid; using 1 s (allowed 0-60 s)\n");
        }
    }

    if (valid && m3.type != TOML_UNKNOWN) {
        M3Config parsed_m3{};
        std::array<std::uint8_t, 6> parsed_mac{};
        const bool m3_valid = m3.type == TOML_TABLE && copy_text(printer_name, parsed_m3.printer_name) &&
                              copy_text(pin, parsed_m3.pin) && parse_mac(mac, parsed_mac);
        if (m3_valid) {
            parsed_m3.mac = parsed_mac;
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
