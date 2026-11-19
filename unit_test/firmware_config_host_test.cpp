// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "config/firmware_config.h"

namespace {

const std::string BASE_CONFIG =
    "format_version = 1\n"
    "[display]\n"
    "palette = \"white\"\n"
    "backlight_percent = 75\n";

config::FirmwareConfig parse(const std::string& contents) {
    config::FirmwareConfig result;
    assert(config::parse_firmware_config(contents, result));
    assert(result.display.palette_key == display::WHITE_PALETTE_KEY);
    assert(result.display.backlight_percent == 75);
    return result;
}

}  // namespace

int main() {
    static_assert(gbp::host::valid_rx_sample_delay_us(2));
    static_assert(gbp::host::valid_rx_sample_delay_us(10));
    static_assert(!gbp::host::valid_rx_sample_delay_us(1));
    static_assert(!gbp::host::valid_rx_sample_delay_us(11));

    assert(parse(BASE_CONFIG).m2.rx_sample_delay_us == 5);
    assert(parse(BASE_CONFIG).m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[M1]\nAutoCutTimeLimit = 0\n").m1.auto_cut_time_limit_us == 0);
    assert(parse(BASE_CONFIG + "\n[M1]\nAutoCutTimeLimit = 0.25\n").m1.auto_cut_time_limit_us == 250'000);
    assert(parse(BASE_CONFIG + "\n[M1]\nAutoCutTimeLimit = 60\n").m1.auto_cut_time_limit_us == 60'000'000);
    assert(parse(BASE_CONFIG + "\n[M1]\nAutoCutTimeLimit = -1\n").m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[M1]\nAutoCutTimeLimit = 61\n").m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[M1]\nAutoCutTimeLimit = \"1\"\n").m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[m2]\nrx_sample_delay_us = 2\n").m2.rx_sample_delay_us == 2);
    assert(parse(BASE_CONFIG + "\n[m2]\nrx_sample_delay_us = 10\n").m2.rx_sample_delay_us == 10);
    assert(parse(BASE_CONFIG + "\n[m2]\nrx_sample_delay_us = 0\n").m2.rx_sample_delay_us == 5);
    assert(parse(BASE_CONFIG + "\n[m2]\nrx_sample_delay_us = 11\n").m2.rx_sample_delay_us == 5);
    assert(parse(BASE_CONFIG + "\n[m2]\nrx_sample_delay_us = \"5\"\n").m2.rx_sample_delay_us == 5);
    assert(parse("format_version = 1\nm2 = 5\n[display]\npalette = \"white\"\n"
                 "backlight_percent = 75\n").m2.rx_sample_delay_us == 5);

    std::ifstream example{"config.example.toml"};
    assert(example);
    const std::string example_text{std::istreambuf_iterator<char>{example}, std::istreambuf_iterator<char>{}};
    config::FirmwareConfig example_config;
    assert(config::parse_firmware_config(example_text, example_config));
    assert(example_config.display.backlight_percent == 50);
    assert(example_config.m1.auto_cut_time_limit_us == 1'000'000);
    assert(example_config.m2.rx_sample_delay_us == 5);
    assert(example_config.m3.printer_name[0] == 'Q');
    assert(example_config.m3.pin[0] == '0');
    assert(example_config.m3.mac[0] == 0x86);
    const auto configured = parse(BASE_CONFIG + "\n[m3]\nprinter_name = \"Qsprinter\"\n"
                                  "pin = \"0000\"\nmac = \"86:67:7A:D8:36:6E\"\n");
    assert(configured.m3.mac[2] == 0x7a && configured.m3.mac[5] == 0x6e);
    const auto fallback = parse(BASE_CONFIG + "\n[m3]\nprinter_name = \"Wrong\"\n"
                                "pin = \"1234\"\nmac = \"bad\"\n");
    assert(fallback.m3.printer_name[0] == 'Q' && fallback.m3.pin[0] == '0');

    config::FirmwareConfig unchanged;
    unchanged.m2.rx_sample_delay_us = 8;
    assert(!config::parse_firmware_config("format_version = 1\n", unchanged));
    assert(unchanged.m2.rx_sample_delay_us == 8);
}
