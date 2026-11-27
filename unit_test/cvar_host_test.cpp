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
#include "storage/sd_card.h"

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
    std::array<char, MAX_CONFIG_BYTES> updated{};
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
    "paper_type = \"label\"\nlabel_length_mm = 75\nlabel_gap_mm = 2\nprotocol = \"tspl\"\n"
    "print_direction = 0\n"
    "centering = \"firmware\"\n"
    "[m3.advance]\nprintable_width_dots_58mm = 360\nimage_scale_58mm = 2\n"
    "printable_width_dots_80mm = 512\nimage_scale_80mm = 3\n"
    "printable_width_dots_110mm = 831\nimage_scale_110mm = 5\n";

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
    constexpr usize variable_count = 62;
    assert(controller.registered_count() == variable_count);
    assert(controller.find("m3.pin") == &Config::Vars::pin);
    assert(controller.find("m3.label_gap_mm") == &Config::Vars::label_gap_mm);
    assert(controller.find("m3.print_direction") == &Config::Vars::print_direction);
    assert(controller.find("missing") == nullptr);
    {
        Config::CVar<uint8> temporary{"test.value", 3, decode_test, clamp_test, encode_test};
        assert(controller.registered_count() == variable_count + 1);
        assert(controller.find("test.value") == &temporary);
        assert(temporary.set(20));
        assert(temporary.get() == 10);
    }
    assert(controller.registered_count() == variable_count);
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
    assert(m3.paper_type == Config::PaperType::Label);
    assert(m3.label_length_mm == 75);
    assert(m3.label_gap_mm == 2);
    assert(m3.protocol == Config::PrintProtocol::Tspl);
    assert(m3.print_direction == 0);
    assert(m3.centering == Config::CenteringMode::Firmware);
    assert(std::string_view{m3.printer_name.data()} == "Printer X");
    assert(std::string_view{m3.pin.data()} == "2468");
    assert(m3.resolved_advance().printable_width_dots == 512);
    assert(m3.resolved_advance().image_scale_quarters == Config::SCALE_3X);
    assert(m3.advance.mm58.printable_width_dots == 360);
    assert(m3.advance.mm110.printable_width_dots == 831);
    assert(m3.advance.mm110.image_scale_quarters == Config::SCALE_5X);

    assert(!controller.load_text("format_version = 1\n[display\n"));
    assert(!controller.load_text("format_version = 2\n"));
    assert(Config::Vars::backlight_percent.get() == 75);
    assert(Config::Vars::printer_type.get() == Config::PrinterType::Mm80);

    assert(controller.load_text("format_version = 1\n[display]\npalette = \"bogus\"\n"
                                "backlight_percent = -1\n[m2]\nrx_sample_delay_us = 11\n"
                                "[m3]\nprinter_name = \"\"\npin = \"bad\\n\"\n"
                                "printer_type = \"bogus\"\ncentering = \"bogus\"\n[m3.advance]\n"
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
    assert(fallback.resolved_advance().image_scale_quarters == Config::SCALE_2X);
    assert(fallback.paper_type == Config::PaperType::Roll);
    assert(fallback.protocol == Config::PrintProtocol::EscPos);
    assert(fallback.print_direction == 1);
    assert(fallback.centering == Config::CenteringMode::Printer);
    assert(fallback.label_length_mm == 50);
    assert(fallback.label_gap_mm == 0);

    assert(controller.load_text("format_version = 1\n[m3]\nprotocol = \"tspl\"\nlabel_gap_mm = 50\n"));
    assert(Config::Vars::print_protocol.get() == Config::PrintProtocol::Tspl);
    assert(Config::Vars::label_gap_mm.get() == 50);
    assert(Config::Vars::current_m3_config().protocol == Config::PrintProtocol::Tspl);
    assert(Config::Vars::current_m3_config().label_gap_mm == 0);

    assert(controller.load_text("format_version = 1\n[m3]\nprinter_type = \"110mm\"\n"
                                "paper_type = \"label\"\nlabel_length_mm = 100\nprotocol = \"escpos\"\n"
                                "[m3.advance]\nimage_scale_110mm = 1\n"));
    const auto wide = Config::Vars::current_m3_config();
    assert(wide.resolved_advance().printable_width_dots == 831);
    assert(wide.resolved_advance().image_scale_quarters == Config::SCALE_1X);
    assert(wide.paper_type == Config::PaperType::Label && wide.label_length_mm == 100);
    assert(wide.centering == Config::CenteringMode::Printer);

    assert(controller.load_text("format_version = 1\n[m3]\nprinter_type = \"110mm\"\n"
                                "[m3.advance]\nimage_scale_110mm = 4\n"));
    assert(Config::Vars::current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_4X);

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
    assert(Config::Vars::label_gap_mm.set(5, Config::WriteMode::RuntimeAndToml));
    assert(Fixture::document.find("label_gap_mm = 5") != std::string::npos);
    assert(Config::Vars::print_protocol.set(Config::PrintProtocol::Tspl, Config::WriteMode::RuntimeAndToml));
    assert(Fixture::document.find("protocol = \"tspl\"") != std::string::npos);
    assert(Config::Vars::print_direction.set(0, Config::WriteMode::RuntimeAndToml));
    assert(Fixture::document.find("print_direction = 0") != std::string::npos);
    assert(Config::Vars::centering.set(Config::CenteringMode::Printer, Config::WriteMode::RuntimeAndToml));
    assert(Fixture::document.find("centering = \"printer\"") != std::string::npos);
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
    assert(Config::Vars::current_m3_config().centering == Config::CenteringMode::Printer);
    assert(Config::Vars::current_m3_config().protocol == Config::PrintProtocol::Tspl);
    assert(Config::Vars::current_m3_config().print_direction == 0);
    assert(Config::Vars::current_m3_config().label_gap_mm == 5);
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

void test_scale_fallback_and_persistence() {
    using Config::PrinterType;
    using namespace Config::Vars;
    constexpr auto persist = Config::WriteMode::RuntimeAndToml;
    auto& controller = Config::Controller::instance();
    Fixture::document =
        "format_version = 1\n[m3]\nprinter_type = \"110mm\"\n"
        "[m3.advance]\nimage_scale_110mm = 5\nimage_scale_80mm = 5\n"
        "image_scale_37mm = 2\nprintable_width_dots_37mm = 168\n";
    assert(controller.reload());
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_5X);
    assert(printer_type.set(PrinterType::Mm80, persist));
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_3X);
    assert(set_m3_image_scale(Config::SCALE_2X, persist));
    assert(Fixture::document.find("image_scale_80mm = 2") != std::string::npos);
    assert(Fixture::document.find("image_scale_110mm = 5") != std::string::npos);
    assert(controller.reload());
    assert(printer_type.get() == PrinterType::Mm80);
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_2X);

    assert(printer_type.set(PrinterType::Mm37, persist));
    assert(controller.reload());
    assert(printer_type.get() == PrinterType::Mm37);
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_1X);
    assert(current_m3_config().resolved_advance().printable_width_dots == 168);
    assert(!set_m3_image_scale(Config::SCALE_2X, persist));
    assert(printer_type.set(PrinterType::Mm110, persist));
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_5X);

    std::ifstream example{"config.example.toml"};
    assert(example);
    Fixture::document.assign(std::istreambuf_iterator<char>{example}, std::istreambuf_iterator<char>{});
    assert(controller.reload());
    assert(printer_type.set(PrinterType::Mm37, persist));
    assert(set_m3_image_scale(Config::SCALE_1X, persist));
    assert(Fixture::document.find("[m3.advance]\nimage_scale_37mm = 1") != std::string::npos);
    assert(controller.reload());
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_1X);
}

void test_fractional_scale_and_margin_slots() {
    using namespace Config::Vars;
    constexpr auto persist = Config::WriteMode::RuntimeAndToml;
    auto& controller = Config::Controller::instance();
    Fixture::document =
        "format_version = 1\n[m3]\nprinter_type = \"37mm\"\ncentering = \"firmware\"\n"
        "[m3.advance]\nimage_scale_37mm = 1.5\n"
        "left_margin_dots_37mm_1_5x = 7\nleft_margin_dots_110mm_5x = 28\n";
    assert(controller.reload());
    auto advance = current_m3_config().resolved_advance();
    assert(advance.printable_width_dots == 280);
    assert(advance.image_scale_quarters == Config::SCALE_1_5X);
    assert(advance.left_margin_dots == 7);
    assert(set_m3_left_margin_dots(9, persist));
    assert(Fixture::document.find("[m3.left_margin_dots.escpos]\n37mm_1_5x = 9") != std::string::npos);
    assert(Fixture::document.find("left_margin_dots_37mm_1_5x = 7") != std::string::npos);
    Fixture::writable = false;
    assert(!set_m3_left_margin_dots(10, persist));
    Fixture::writable = true;
    assert(current_m3_config().resolved_advance().left_margin_dots == 9);

    assert(set_m3_image_scale(Config::SCALE_1X, persist));
    assert(Fixture::document.find("image_scale_37mm = 1") != std::string::npos);
    assert(current_m3_config().resolved_advance().printable_width_dots == 280);
    assert(current_m3_config().resolved_advance().left_margin_dots == 60);
    assert(set_m3_left_margin_dots(4, persist));
    assert(printer_type.set(Config::PrinterType::Mm110, persist));
    assert(current_m3_config().resolved_advance().left_margin_dots == 28);
    assert(set_m3_left_margin_dots(30, persist));
    assert(printer_type.set(Config::PrinterType::Mm37, persist));
    assert(current_m3_config().resolved_advance().left_margin_dots == 4);
    assert(set_m3_image_scale(Config::SCALE_1_5X, persist));
    assert(Fixture::document.find("image_scale_37mm = 1.5") != std::string::npos);
    assert(current_m3_config().resolved_advance().left_margin_dots == 9);
    assert(controller.reload());
    assert(current_m3_config().resolved_advance().left_margin_dots == 9);
    assert(print_protocol.set(Config::PrintProtocol::Tspl));
    assert(print_direction.set(0));
    assert(current_m3_config().resolved_advance().left_margin_dots == 7);
    assert(print_direction.set(1));
    assert(current_m3_config().resolved_advance().left_margin_dots == 7);
    assert(print_protocol.set(Config::PrintProtocol::EscPos));
    assert(printer_type.set(Config::PrinterType::Mm110));
    assert(current_m3_config().resolved_advance().left_margin_dots == 30);

    assert(controller.load_text("format_version = 1\n[m3]\nprinter_type = \"37mm\"\n"
                                "[m3.advance]\nprintable_width_dots_37mm = 168\n"
                                "image_scale_37mm = 1.5\n"));
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_1X);
    assert(current_m3_config().resolved_advance().left_margin_dots == 4);
    assert(!set_m3_image_scale(Config::SCALE_1_5X, Config::WriteMode::RuntimeOnly));
    assert(controller.load_text("format_version = 1\n[m3]\nprinter_type = \"58mm\"\n"
                                "[m3.advance]\nimage_scale_58mm = 1.5\n"));
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_1_5X);
    assert(current_m3_config().resolved_advance().left_margin_dots == 72);
    assert(controller.load_text("format_version = 1\n[m3]\nprinter_type = \"80mm\"\n"
                                "[m3.advance]\nimage_scale_80mm = 1.5\n"));
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_3X);
    assert(controller.load_text("format_version = 1\n[m3]\nprinter_type = \"37mm\"\n"
                                "[m3.advance]\nimage_scale_37mm = 1.25\n"
                                "left_margin_dots_37mm_1_25x = 45\n"));
    assert(current_m3_config().resolved_advance().image_scale_quarters == Config::SCALE_1X);
    assert(current_m3_config().resolved_advance().left_margin_dots == 60);
}

void test_all_margin_slots() {
    using Config::PrintProtocol;
    using Config::PrinterType;
    using namespace Config::Vars;
    constexpr auto persist = Config::WriteMode::RuntimeAndToml;
    constexpr auto runtime = Config::WriteMode::RuntimeOnly;
    struct Slot { Config::PrinterType type; uint8 scale; uint16 margin; };
    constexpr Slot slots[]{
        {Config::PrinterType::Mm37, Config::SCALE_1X, 1},
        {Config::PrinterType::Mm37, Config::SCALE_1_5X, 2},
        {Config::PrinterType::Mm58, Config::SCALE_1X, 3},
        {Config::PrinterType::Mm58, Config::SCALE_1_5X, 4},
        {Config::PrinterType::Mm58, Config::SCALE_2X, 13},
        {Config::PrinterType::Mm80, Config::SCALE_1X, 5},
        {Config::PrinterType::Mm80, Config::SCALE_2X, 6},
        {Config::PrinterType::Mm80, Config::SCALE_3X, 7},
        {Config::PrinterType::Mm110, Config::SCALE_1X, 8},
        {Config::PrinterType::Mm110, Config::SCALE_2X, 9},
        {Config::PrinterType::Mm110, Config::SCALE_3X, 10},
        {Config::PrinterType::Mm110, Config::SCALE_4X, 11},
        {Config::PrinterType::Mm110, Config::SCALE_5X, 12},
    };
    struct Mode { PrintProtocol protocol; uint8 direction; uint16 offset; };
    constexpr Mode modes[]{
        {PrintProtocol::EscPos, 0, 0},
        {PrintProtocol::Tspl, 0, 100},
        {PrintProtocol::Tspl, 1, 200},
    };
    std::ifstream example{"config.example.toml", std::ios::binary};
    assert(example);
    Fixture::document.assign(std::istreambuf_iterator<char>{example}, std::istreambuf_iterator<char>{});
    constexpr usize legacy_config_limit = 4096;
    assert(Fixture::document.size() + 3 <= legacy_config_limit);
    Fixture::document += "\n#";
    Fixture::document.append(legacy_config_limit - Fixture::document.size() - 1, ' ');
    Fixture::document += '\n';
    assert(Config::Controller::instance().reload());
    for (const auto& mode : modes) {
        assert(print_protocol.set(mode.protocol, persist));
        assert(print_direction.set(mode.direction, persist));
        for (const auto& slot : slots) {
            assert(printer_type.set(slot.type, persist));
            assert(set_m3_image_scale(slot.scale, persist));
            assert(set_m3_left_margin_dots(slot.margin + mode.offset, persist));
        }
    }
    assert(Fixture::document.find("[m3.left_margin_dots.escpos]\n37mm_1x = 1") != std::string::npos);
    assert(Fixture::document.find("[m3.left_margin_dots.tspl_dir0]\n37mm_1x = 101") != std::string::npos);
    assert(Fixture::document.find("[m3.left_margin_dots.tspl_dir1]\n37mm_1x = 201") != std::string::npos);
    assert(Config::Controller::instance().reload());
    for (const auto& mode : modes) {
        assert(print_protocol.set(mode.protocol, runtime));
        assert(print_direction.set(mode.direction, runtime));
        for (const auto& slot : slots) {
            assert(printer_type.set(slot.type, runtime));
            assert(set_m3_image_scale(slot.scale, runtime));
            assert(current_m3_config().resolved_advance().left_margin_dots == slot.margin + mode.offset);
        }
    }
    auto* escpos_margin = find_m3_left_margin_var(PrinterType::Mm58, Config::SCALE_2X, PrintProtocol::EscPos, 0);
    assert(escpos_margin != nullptr);
    assert(escpos_margin == find_m3_left_margin_var(PrinterType::Mm58, Config::SCALE_2X, PrintProtocol::EscPos, 1));
    assert(!escpos_margin->set(Config::AUTO_LEFT_MARGIN, persist));
    assert(find_m3_left_margin_var(PrinterType::Mm58, Config::SCALE_2X, PrintProtocol::Tspl, 2) == nullptr);
    assert(find_m3_left_margin_var(PrinterType::Mm37, Config::SCALE_2X, PrintProtocol::Tspl, 0) == nullptr);
}

void test_default_left_margins() {
    using Config::M3Config;
    using Config::PrinterType;
    struct Case { PrinterType type; uint8 scale; uint16 width; uint16 margin; };
    constexpr Case cases[]{
        {PrinterType::Mm37, Config::SCALE_1X, 280, 60},
        {PrinterType::Mm37, Config::SCALE_1_5X, 280, 20},
        {PrinterType::Mm58, Config::SCALE_1X, 384, 112},
        {PrinterType::Mm58, Config::SCALE_1_5X, 384, 72},
        {PrinterType::Mm58, Config::SCALE_2X, 384, 32},
        {PrinterType::Mm80, Config::SCALE_1X, 576, 208},
        {PrinterType::Mm80, Config::SCALE_2X, 576, 128},
        {PrinterType::Mm80, Config::SCALE_3X, 576, 48},
        {PrinterType::Mm110, Config::SCALE_1X, 831, 335},
        {PrinterType::Mm110, Config::SCALE_2X, 831, 255},
        {PrinterType::Mm110, Config::SCALE_3X, 831, 175},
        {PrinterType::Mm110, Config::SCALE_4X, 831, 95},
        {PrinterType::Mm110, Config::SCALE_5X, 831, 15},
    };
    for (const auto& entry : cases) {
        M3Config settings{};
        settings.printer_type = entry.type;
        switch (entry.type) {
            case PrinterType::Mm37: settings.advance.mm37.image_scale_quarters = entry.scale; break;
            case PrinterType::Mm58: settings.advance.mm58.image_scale_quarters = entry.scale; break;
            case PrinterType::Mm80: settings.advance.mm80.image_scale_quarters = entry.scale; break;
            case PrinterType::Mm110: settings.advance.mm110.image_scale_quarters = entry.scale; break;
            case PrinterType::Count: assert(false); break;
        }
        const auto resolved = settings.resolved_advance();
        assert(resolved.printable_width_dots == entry.width);
        assert(resolved.left_margin_dots == entry.margin);
    }
}

}  // namespace

int main() {
    test_registration();
    test_loading();
    test_set_and_reload();
    test_document_update();
    test_scale_fallback_and_persistence();
    test_fractional_scale_and_margin_slots();
    test_all_margin_slots();
    test_default_left_margins();
}
