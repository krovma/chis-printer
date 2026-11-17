// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "config/firmware_config.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "tomlc17.h"

namespace config {
namespace {

constexpr std::array<std::string_view, display::PALETTE_COUNT> PALETTE_NAMES{
    "red", "yellow", "green", "cyan", "blue", "magenta", "white",
};

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
    const toml_datum_t m2 = toml_get(document.toptab, "m2");
    const toml_datum_t rx_delay = toml_seek(document.toptab, "m2.rx_sample_delay_us");

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

    toml_free(document);
    if (valid) {
        output = parsed;
    }
    return valid;
}

}  // namespace config
