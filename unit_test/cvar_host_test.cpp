// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "config/cvar.h"
#include "config/toml_document.h"
#include "config/variables.h"

namespace Fixture {
std::string document;
bool readable = true;
bool writable = true;
}  // namespace Fixture

namespace Storage {
bool read_config_text(std::string_view& output) {
    if (!Fixture::readable) return false;
    output = Fixture::document;
    return true;
}

bool write_config_value(const std::string_view path, const std::string_view value) {
    if (!Fixture::writable) return false;
    std::array<char, 4096> updated{};
    usize size = 0;
    if (!Config::update_toml_value(Fixture::document, path, value,
                                   updated.data(), updated.size(), size)) return false;
    Fixture::document.assign(updated.data(), size);
    return true;
}
}  // namespace Storage

namespace {

constexpr std::string_view CONFIG =
    "format_version = 1\n"
    "[ui]\nlocale = \"zh\"\n"
    "[display] # LCD\npalette = \"cyan\" # tint\nbacklight_percent = 75\n"
    "[m1]\nauto_cut_time_limit = 0.25\nsimulated_print_time = 2.5\n"
    "[m2]\nrx_sample_delay_us = 8\n"
    "[m3]\nprinter_name = \"Printer X\"\npin = \"2468\"\nprinter_type = \"80mm\"\n"
    "[m3.advance]\nprintable_width_dots_58mm = 360\nimage_scale_58mm = 2\n"
    "printable_width_dots_80mm = 512\nimage_scale_80mm = 3\n";

bool decode_test(const toml_datum_t datum, uint8& output) {
    if (datum.type != TOML_INT64 || datum.u.int64 < 0 || datum.u.int64 > 10) return false;
    output = static_cast<uint8>(datum.u.int64);
    return true;
}

bool clamp_test(uint8& value) {
    if (value > 10) value = 10;
    return true;
}

bool encode_test(const uint8& value, char* output, const usize capacity) {
    const int length = std::snprintf(output, capacity, "%u", static_cast<unsigned>(value));
    return length > 0 && static_cast<usize>(length) < capacity;
}

void test_registration() {
    auto& controller = Config::Controller::instance();
    assert(controller.registered_count() == 13);
    assert(controller.find("m3.pin") == &Config::Vars::pin);
    assert(controller.find("missing") == nullptr);
    {
        Config::CVar<uint8> temporary{"test.value", 3, decode_test, clamp_test, encode_test};
        assert(controller.registered_count() == 14);
        assert(controller.find("test.value") == &temporary);
        assert(temporary.set(20));
        assert(temporary.get() == 10);
    }
    assert(controller.registered_count() == 13);
    assert(controller.find("test.value") == nullptr);
}

void test_loading() {
    auto& controller = Config::Controller::instance();
    assert(controller.load_text(CONFIG));
    assert(Config::Vars::locale.get() == Display::Ui::Locale::Chinese);
    assert(Config::Vars::palette.get() == 3);
    assert(Config::Vars::backlight_percent.get() == 75);
    assert(Config::Vars::auto_cut_time_limit_us.get() == 250'000);
    assert(Config::Vars::simulated_print_time_us.get() == 2'500'000);
    assert(Config::Vars::rx_sample_delay_us.get() == 8);
    const auto m3 = Config::Vars::current_m3_config();
    assert(m3.printer_type == Config::PrinterType::Mm80);
    assert(std::string_view{m3.printer_name.data()} == "Printer X");
    assert(std::string_view{m3.pin.data()} == "2468");
    assert(m3.resolved_advance().printable_width_dots == 512);
    assert(m3.resolved_advance().image_scale == 3);
    assert(m3.advance.mm58.printable_width_dots == 360);

    assert(!controller.load_text("format_version = 1\n[display\n"));
    assert(!controller.load_text("format_version = 2\n"));
    assert(Config::Vars::backlight_percent.get() == 75);
    assert(Config::Vars::printer_type.get() == Config::PrinterType::Mm80);

    assert(controller.load_text("format_version = 1\n[display]\npalette = \"bogus\"\n"
                                "backlight_percent = -1\n[m2]\nrx_sample_delay_us = 11\n"
                                "[m3]\nprinter_name = \"\"\npin = \"bad\\n\"\n"
                                "printer_type = \"bogus\"\n[m3.advance]\n"
                                "printable_width_dots_80mm = 479\nimage_scale_80mm = 1\n"));
    assert(Config::Vars::locale.get() == Display::Ui::DEFAULT_LOCALE);
    assert(Config::Vars::palette.get() == Display::WHITE_PALETTE_KEY);
    assert(Config::Vars::backlight_percent.get() == 50);
    assert(Config::Vars::rx_sample_delay_us.get() == 5);
    const auto fallback = Config::Vars::current_m3_config();
    assert(std::string_view{fallback.printer_name.data()} == "Qsprinter");
    assert(std::string_view{fallback.pin.data()} == "0000");
    assert(fallback.printer_type == Config::PrinterType::Mm58);
    assert(fallback.resolved_advance().printable_width_dots == 384);
    assert(fallback.resolved_advance().image_scale == 2);

    assert(controller.load_text("format_version = 1\n[M1]\nAutoCutTimeLimit = 0.5\n"
                                "SimulatedPrintTime = 0.75\n"));
    assert(Config::Vars::auto_cut_time_limit_us.get() == 500'000);
    assert(Config::Vars::simulated_print_time_us.get() == 750'000);

    std::ifstream example{"config.example.toml"};
    assert(example);
    const std::string text{std::istreambuf_iterator<char>{example}, std::istreambuf_iterator<char>{}};
    assert(controller.load_text(text));
    assert(Config::Vars::backlight_percent.get() == 50);
    assert(Config::Vars::current_m3_config().resolved_advance().printable_width_dots == 384);
}

void test_set_and_reload() {
    auto& controller = Config::Controller::instance();
    Fixture::document = CONFIG;
    assert(controller.reload());
    assert(Config::Vars::auto_cut_time_limit_us.set(70'000'000));
    assert(Config::Vars::auto_cut_time_limit_us.get() == 60'000'000);
    assert(Fixture::document.find("auto_cut_time_limit = 0.25") != std::string::npos);
    assert(!Config::Vars::locale.set(static_cast<Display::Ui::Locale>(255)));
    assert(!Config::Vars::printer_type.set(static_cast<Config::PrinterType>(255)));
    assert(Config::Vars::locale.get() == Display::Ui::Locale::Chinese);

    Fixture::writable = false;
    assert(!Config::Vars::backlight_percent.set(90, Config::WriteMode::RuntimeAndToml));
    assert(Config::Vars::backlight_percent.get() == 75);
    Fixture::writable = true;
    assert(Config::Vars::auto_cut_time_limit_us.set(3'500'000, Config::WriteMode::RuntimeAndToml));
    assert(Fixture::document.find("auto_cut_time_limit = 3.5") != std::string::npos);
    assert(Fixture::document.find("printer_name = \"Printer X\"") != std::string::npos);
    assert(Fixture::document.find("pin = \"2468\"") != std::string::npos);
    assert(Fixture::document.find("printable_width_dots_80mm = 512") != std::string::npos);
    assert(Config::Vars::palette.set(0, Config::WriteMode::RuntimeAndToml));
    assert(Fixture::document.find("palette = \"red\" # tint") != std::string::npos);
    assert(Fixture::document.find("[display] # LCD") != std::string::npos);
    auto quoted_name = Config::Vars::printer_name.get();
    quoted_name.fill('\0');
    constexpr char QUOTED_NAME[] = "Print \\\"A\\\"";
    for (usize index = 0; index < sizeof(QUOTED_NAME); ++index) quoted_name[index] = QUOTED_NAME[index];
    assert(Config::Vars::printer_name.set(quoted_name, Config::WriteMode::RuntimeAndToml));
    assert(Fixture::document.find("printer_name = \"Print \\\\\\\"A\\\\\\\"\"") != std::string::npos);
    assert(Config::Vars::backlight_percent.set(200, Config::WriteMode::RuntimeAndToml));
    assert(Config::Vars::backlight_percent.get() == 100);
    assert(Fixture::document.find("backlight_percent = 100") != std::string::npos);

    assert(Config::Vars::palette.set(6));
    assert(controller.reload());
    assert(Config::Vars::palette.get() == 0);
    assert(Config::Vars::auto_cut_time_limit_us.get() == 3'500'000);
    assert(std::string_view{Config::Vars::printer_name.get().data()} == QUOTED_NAME);
    Fixture::readable = false;
    assert(!controller.reload());
    assert(Config::Vars::palette.get() == 0);
    Fixture::readable = true;
    Fixture::document = "format_version = 1\n[broken\n";
    assert(!controller.reload());
    assert(Config::Vars::palette.get() == 0);
    controller.reset_defaults();
    assert(Config::Vars::palette.get() == Display::WHITE_PALETTE_KEY);
}

void test_document_update() {
    std::array<char, 512> buffer{};
    usize size = 0;
    assert(Config::update_toml_value({}, "m3.printer_type", "\"80mm\"",
                                     buffer.data(), buffer.size(), size));
    std::string document{buffer.data(), size};
    assert(document.find("format_version = 1") != std::string::npos);
    assert(Config::Controller::instance().load_text(document));
    assert(Config::Vars::printer_type.get() == Config::PrinterType::Mm80);

    const std::string dotted = "format_version = 1\ndisplay.palette = \"white\"\n"
                               "display.backlight_percent = 50\n[m3.advance]\n"
                               "printable_width_dots_58mm = 384\n";
    assert(Config::update_toml_value(dotted, "display.palette", "\"blue\"",
                                     buffer.data(), buffer.size(), size));
    document.assign(buffer.data(), size);
    assert(document.find("display.palette = \"blue\"") != std::string::npos);
    assert(Config::update_toml_value(document, "m3.printer_type", "\"58mm\"",
                                     buffer.data(), buffer.size(), size));
    document.assign(buffer.data(), size);
    assert(document.find("[m3]\nprinter_type = \"58mm\"") != std::string::npos);
    assert(Config::Controller::instance().load_text(document));
    assert(Config::Vars::palette.get() == 4);
    assert(!Config::update_toml_value(document, "m3.printer_type", "\"80mm\"",
                                      buffer.data(), 8, size));
}

}  // namespace

int main() {
    test_registration();
    test_loading();
    test_set_and_reload();
    test_document_update();
}
