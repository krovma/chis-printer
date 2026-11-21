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
const std::string M3_CONNECTION =
    "\n[m3]\nprinter_name = \"Qsprinter\"\n"
    "pin = \"0000\"\n";

Config::FirmwareConfig parse(const std::string& contents) {
    Config::FirmwareConfig result;
    assert(Config::parse_firmware_config(contents, result));
    assert(result.display.palette_key == Display::WHITE_PALETTE_KEY);
    assert(result.display.backlight_percent == 75);
    return result;
}

}  // namespace

int main() {
    static_assert(Gbp::Host::valid_rx_sample_delay_us(2));
    static_assert(Gbp::Host::valid_rx_sample_delay_us(10));
    static_assert(!Gbp::Host::valid_rx_sample_delay_us(1));
    static_assert(!Gbp::Host::valid_rx_sample_delay_us(11));

    assert(parse(BASE_CONFIG).m2.rx_sample_delay_us == 5);
    assert(parse(BASE_CONFIG).m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG).m1.simulated_print_time_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nauto_cut_time_limit = 0\n").m1.auto_cut_time_limit_us == 0);
    assert(parse(BASE_CONFIG + "\n[m1]\nauto_cut_time_limit = 0.25\n").m1.auto_cut_time_limit_us == 250'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nauto_cut_time_limit = 60\n").m1.auto_cut_time_limit_us == 60'000'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nauto_cut_time_limit = -1\n").m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nauto_cut_time_limit = 61\n").m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nauto_cut_time_limit = \"1\"\n").m1.auto_cut_time_limit_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nsimulated_print_time = 0\n").m1.simulated_print_time_us == 0);
    assert(parse(BASE_CONFIG + "\n[m1]\nsimulated_print_time = 0.75\n").m1.simulated_print_time_us == 750'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nsimulated_print_time = 60\n").m1.simulated_print_time_us == 60'000'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nsimulated_print_time = -1\n").m1.simulated_print_time_us == 1'000'000);
    assert(parse(BASE_CONFIG + "\n[m1]\nsimulated_print_time = 61\n").m1.simulated_print_time_us == 1'000'000);
    const auto legacy_m1 = parse(BASE_CONFIG + "\n[M1]\nAutoCutTimeLimit = 0.25\n"
                                 "SimulatedPrintTime = 0.75\n");
    assert(legacy_m1.m1.auto_cut_time_limit_us == 250'000);
    assert(legacy_m1.m1.simulated_print_time_us == 750'000);
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
    Config::FirmwareConfig example_config;
    assert(Config::parse_firmware_config(example_text, example_config));
    assert(example_config.display.backlight_percent == 50);
    assert(example_config.m1.auto_cut_time_limit_us == 1'000'000);
    assert(example_config.m1.simulated_print_time_us == 1'000'000);
    assert(example_config.m2.rx_sample_delay_us == 5);
    assert(example_config.m3.printer_name[0] == 'Q');
    assert(example_config.m3.pin[0] == '0');
    assert(example_config.m3.printer_type == Config::PrinterType::Mm58);
    assert(example_config.m3.resolved_advance().printable_width_dots == 384);
    assert(example_config.m3.resolved_advance().image_scale == 2);
    const auto configured = parse(BASE_CONFIG + M3_CONNECTION + "printer_type = \"80mm\"\n");
    assert(configured.m3.printer_name[0] == 'Q' && configured.m3.pin[0] == '0');
    assert(configured.m3.printer_type == Config::PrinterType::Mm80);
    assert(configured.m3.resolved_advance().printable_width_dots == 576);
    assert(configured.m3.resolved_advance().image_scale == 3);
    assert(parse(BASE_CONFIG + M3_CONNECTION).m3.resolved_advance().image_scale == 2);
    assert(parse(BASE_CONFIG + M3_CONNECTION + "printer_type = \"unknown\"\n")
               .m3.resolved_advance().image_scale == 2);
    const std::string overrides = "[m3.advance]\nprintable_width_dots_58mm = 360\nimage_scale_58mm = 2\n"
                                  "printable_width_dots_80mm = 512\nimage_scale_80mm = 3\n";
    const auto advanced_58mm = parse(BASE_CONFIG + M3_CONNECTION + "printer_type = \"58mm\"\n" + overrides);
    assert(advanced_58mm.m3.resolved_advance().printable_width_dots == 360);
    assert(advanced_58mm.m3.resolved_advance().image_scale == 2);
    assert(advanced_58mm.m3.advance.mm80.printable_width_dots == 512);
    const auto advanced_80mm = parse(BASE_CONFIG + M3_CONNECTION + "printer_type = \"80mm\"\n" + overrides);
    assert(advanced_80mm.m3.resolved_advance().printable_width_dots == 512);
    assert(advanced_80mm.m3.resolved_advance().image_scale == 3);
    assert(advanced_80mm.m3.advance.mm58.printable_width_dots == 360);
    assert(parse(BASE_CONFIG + M3_CONNECTION + "printer_type = \"58mm\"\n"
                 "[m3.advance]\nimage_scale_58mm = 3\n").m3.resolved_advance().image_scale == 2);
    assert(parse(BASE_CONFIG + M3_CONNECTION + "printer_type = \"80mm\"\n"
                 "[m3.advance]\nimage_scale_80mm = 1\n").m3.resolved_advance().image_scale == 3);
    assert(parse(BASE_CONFIG + M3_CONNECTION + "[m3.advance]\nprintable_width_dots_58mm = 319\n")
               .m3.resolved_advance().printable_width_dots == 384);
    assert(parse(BASE_CONFIG + M3_CONNECTION + "printer_type = \"80mm\"\n"
                 "[m3.advance]\nprintable_width_dots_80mm = 479\n")
               .m3.resolved_advance().printable_width_dots == 576);
    const auto fallback = parse(BASE_CONFIG + "\n[m3]\nprinter_name = \"Wrong\"\n"
                                "pin = \"\"\n");
    assert(fallback.m3.printer_name[0] == 'Q' && fallback.m3.pin[0] == '0');

    Config::FirmwareConfig unchanged;
    unchanged.m2.rx_sample_delay_us = 8;
    assert(!Config::parse_firmware_config("format_version = 1\n", unchanged));
    assert(unchanged.m2.rx_sample_delay_us == 8);
}
