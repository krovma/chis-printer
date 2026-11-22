// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "config/variables.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "types.h"
#include "display/zjy154s0800.h"
#include "gbp/host/link_timing.h"
#include "gbr/format.h"

namespace Config::Vars {
namespace {

constexpr std::array<std::string_view, 3> LOCALES{"en", "zh", "ja"};
constexpr std::array<std::string_view, Display::PALETTE_COUNT> PALETTES{
    "red", "yellow", "green", "cyan", "blue", "magenta", "white",
};
constexpr std::array<char, 32> DEFAULT_PRINTER_NAME{{'Q', 's', 'p', 'r', 'i', 'n', 't', 'e', 'r', '\0'}};
constexpr std::array<char, 8> DEFAULT_PIN{{'0', '0', '0', '0', '\0'}};

template <typename T, T Minimum, T Maximum>
bool decode_integer(const toml_datum_t datum, T& output) {
    if (datum.type != TOML_INT64 || datum.u.int64 < Minimum || datum.u.int64 > Maximum) return false;
    output = static_cast<T>(datum.u.int64);
    return true;
}

template <typename T, T Minimum, T Maximum>
bool clamp_integer(T& value) {
    if (value < Minimum) value = Minimum;
    if (value > Maximum) value = Maximum;
    return true;
}

template <typename T>
bool encode_integer(const T& value, char* output, const usize capacity) {
    const int length = std::snprintf(output, capacity, "%u", static_cast<unsigned>(value));
    return length > 0 && static_cast<usize>(length) < capacity;
}

bool decode_locale(const toml_datum_t datum, Display::Ui::Locale& output) {
    if (datum.type != TOML_STRING) return false;
    const std::string_view value{datum.u.str.ptr, static_cast<usize>(datum.u.str.len)};
    for (usize index = 0; index < LOCALES.size(); ++index) {
        if (value == LOCALES[index]) {
            output = static_cast<Display::Ui::Locale>(index);
            return true;
        }
    }
    return false;
}

bool normalize_locale(Display::Ui::Locale& value) {
    return static_cast<usize>(value) < LOCALES.size();
}

bool encode_locale(const Display::Ui::Locale& value, char* output, const usize capacity) {
    const auto index = static_cast<usize>(value);
    if (index >= LOCALES.size()) return false;
    const int length = std::snprintf(output, capacity, "\"%.*s\"",
                                     static_cast<int>(LOCALES[index].size()), LOCALES[index].data());
    return length > 0 && static_cast<usize>(length) < capacity;
}

bool decode_palette(const toml_datum_t datum, uint8& output) {
    if (datum.type != TOML_STRING) return false;
    const std::string_view value{datum.u.str.ptr, static_cast<usize>(datum.u.str.len)};
    for (usize index = 0; index < PALETTES.size(); ++index) {
        if (value == PALETTES[index]) {
            output = static_cast<uint8>(index);
            return true;
        }
    }
    return false;
}

bool encode_palette(const uint8& value, char* output, const usize capacity) {
    if (value >= PALETTES.size()) return false;
    const int length = std::snprintf(output, capacity, "\"%.*s\"",
                                     static_cast<int>(PALETTES[value].size()), PALETTES[value].data());
    return length > 0 && static_cast<usize>(length) < capacity;
}

bool decode_seconds(const toml_datum_t datum, uint32& output) {
    const bool numeric = datum.type == TOML_INT64 || datum.type == TOML_FP64;
    const double seconds = datum.type == TOML_INT64 ? static_cast<double>(datum.u.int64) :
                           datum.type == TOML_FP64 ? datum.u.fp64 : 0.0;
    if (!numeric || !std::isfinite(seconds) || seconds < 0.0 || seconds > 60.0) return false;
    output = static_cast<uint32>(seconds * 1'000'000.0 + 0.5);
    return true;
}

bool clamp_seconds(uint32& value) {
    if (value > 60'000'000u) value = 60'000'000u;
    return true;
}

bool encode_seconds(const uint32& value, char* output, const usize capacity) {
    const unsigned long whole = static_cast<unsigned long>(value / 1'000'000u);
    const unsigned long fraction = static_cast<unsigned long>(value % 1'000'000u);
    if (fraction == 0) {
        const int length = std::snprintf(output, capacity, "%lu", whole);
        return length > 0 && static_cast<usize>(length) < capacity;
    }
    char decimal[7]{};
    std::snprintf(decimal, sizeof(decimal), "%06lu", fraction);
    int digits = 6;
    while (digits > 0 && decimal[digits - 1] == '0') --digits;
    const int length = std::snprintf(output, capacity, "%lu.%.*s", whole, digits, decimal);
    return length > 0 && static_cast<usize>(length) < capacity;
}

bool decode_printer_type(const toml_datum_t datum, PrinterType& output) {
    if (datum.type != TOML_STRING) return false;
    const std::string_view value{datum.u.str.ptr, static_cast<usize>(datum.u.str.len)};
    for (usize index = 0; index < PRINTER_TYPE_COUNT; ++index) {
        if (value == PRINTER_TYPE_NAMES[index]) {
            output = static_cast<PrinterType>(index);
            return true;
        }
    }
    return false;
}

bool normalize_printer_type(PrinterType& value) {
    return static_cast<usize>(value) < PRINTER_TYPE_COUNT;
}

bool encode_printer_type(const PrinterType& value, char* output, const usize capacity) {
    const auto index = static_cast<usize>(value);
    if (index >= PRINTER_TYPE_COUNT) return false;
    const int length = std::snprintf(output, capacity, "\"%s\"", PRINTER_TYPE_NAMES[index]);
    return length > 0 && static_cast<usize>(length) < capacity;
}

bool decode_paper_type(const toml_datum_t datum, PaperType& output) {
    if (datum.type != TOML_STRING) return false;
    const std::string_view value{datum.u.str.ptr, static_cast<usize>(datum.u.str.len)};
    if (value == "roll") output = PaperType::Roll;
    else if (value == "label") output = PaperType::Label;
    else return false;
    return true;
}

bool normalize_paper_type(PaperType& value) {
    return value == PaperType::Roll || value == PaperType::Label;
}

bool encode_paper_type(const PaperType& value, char* output, const usize capacity) {
    if (value != PaperType::Roll && value != PaperType::Label) return false;
    const int length = std::snprintf(output, capacity, "\"%s\"",
                                     value == PaperType::Label ? "label" : "roll");
    return length > 0 && static_cast<usize>(length) < capacity;
}

bool decode_protocol(const toml_datum_t datum, PrintProtocol& output) {
    if (datum.type != TOML_STRING) return false;
    const std::string_view value{datum.u.str.ptr, static_cast<usize>(datum.u.str.len)};
    if (value != "escpos") return false;
    output = PrintProtocol::EscPos;
    return true;
}

bool normalize_protocol(PrintProtocol& value) { return value == PrintProtocol::EscPos; }

bool encode_protocol(const PrintProtocol& value, char* output, const usize capacity) {
    if (value != PrintProtocol::EscPos) return false;
    const int length = std::snprintf(output, capacity, "\"escpos\"");
    return length > 0 && static_cast<usize>(length) < capacity;
}

bool decode_centering(const toml_datum_t datum, CenteringMode& output) {
    if (datum.type != TOML_STRING) return false;
    const std::string_view value{datum.u.str.ptr, static_cast<usize>(datum.u.str.len)};
    if (value == "printer") output = CenteringMode::Printer;
    else if (value == "firmware") output = CenteringMode::Firmware;
    else return false;
    return true;
}

bool normalize_centering(CenteringMode& value) {
    return value == CenteringMode::Printer || value == CenteringMode::Firmware;
}

bool encode_centering(const CenteringMode& value, char* output, const usize capacity) {
    if (value != CenteringMode::Printer && value != CenteringMode::Firmware) return false;
    const int length = std::snprintf(output, capacity, "\"%s\"",
                                     value == CenteringMode::Printer ? "printer" : "firmware");
    return length > 0 && static_cast<usize>(length) < capacity;
}

template <usize N>
bool valid_text(std::array<char, N>& text) {
    usize length = 0;
    while (length < N && text[length] != '\0') {
        const uint8 byte = static_cast<uint8>(text[length]);
        if (byte < 0x20 || byte == 0x7F) return false;
        ++length;
    }
    return length > 0 && length < N;
}

template <usize N>
bool decode_text(const toml_datum_t datum, std::array<char, N>& output) {
    if (datum.type != TOML_STRING || datum.u.str.len <= 0 || static_cast<usize>(datum.u.str.len) >= N) return false;
    if (std::memchr(datum.u.str.ptr, '\0', static_cast<usize>(datum.u.str.len)) != nullptr) return false;
    output.fill('\0');
    std::memcpy(output.data(), datum.u.str.ptr, static_cast<usize>(datum.u.str.len));
    return valid_text(output);
}

template <usize N>
bool encode_text(const std::array<char, N>& value, char* output, const usize capacity) {
    auto copy = value;
    if (!valid_text(copy) || capacity < 3) return false;
    usize offset = 0;
    output[offset++] = '"';
    for (const char ch : value) {
        if (ch == '\0') break;
        if ((ch == '"' || ch == '\\') && offset + 2 >= capacity) return false;
        if (ch == '"' || ch == '\\') output[offset++] = '\\';
        if (offset + 1 >= capacity) return false;
        output[offset++] = ch;
    }
    output[offset++] = '"';
    output[offset] = '\0';
    return true;
}

template <PrinterType Type>
constexpr uint16 MIN_PRINTABLE_WIDTH = Type == PrinterType::Mm37 ? Gbr::Format::GBR_WIDTH :
    scaled_dots(Gbr::Format::GBR_WIDTH, default_m3_advance(Type).image_scale_quarters);

template <PrinterType Type>
bool decode_width(const toml_datum_t datum, uint16& output) {
    return decode_integer<uint16, MIN_PRINTABLE_WIDTH<Type>, 1023>(datum, output);
}

template <PrinterType Type>
bool clamp_width(uint16& value) {
    if (value == 0) return true;
    return clamp_integer<uint16, MIN_PRINTABLE_WIDTH<Type>, 1023>(value);
}

template <PrinterType Type>
bool decode_scale(const toml_datum_t datum, uint8& output) {
    if ((Type == PrinterType::Mm37 || Type == PrinterType::Mm58) &&
        datum.type == TOML_FP64 && datum.u.fp64 == 1.5) {
        output = SCALE_1_5X;
        return true;
    }
    uint8 whole = 0;
    if (!decode_integer<uint8, 1, 5>(datum, whole)) return false;
    output = static_cast<uint8>(whole * SCALE_UNIT);
    return printer_type_supports_scale(Type, output);
}

template <PrinterType Type>
bool normalize_scale(uint8& value) {
    return value == 0 || printer_type_supports_scale(Type, value);
}

bool encode_scale(const uint8& value, char* output, const usize capacity) {
    if (value == SCALE_1_5X) {
        const int length = std::snprintf(output, capacity, "1.5");
        return length > 0 && static_cast<usize>(length) < capacity;
    }
    return value % SCALE_UNIT == 0 &&
           encode_integer<uint8>(static_cast<uint8>(value / SCALE_UNIT), output, capacity);
}

bool normalize_left_margin(uint16& value) {
    return value == AUTO_LEFT_MARGIN || value <= MAX_LEFT_MARGIN_DOTS;
}

}  // namespace

CVar<Display::Ui::Locale> locale{"ui.locale", Display::Ui::DEFAULT_LOCALE,
    decode_locale, normalize_locale, encode_locale};
CVar<uint8> palette{"display.palette", Display::WHITE_PALETTE_KEY,
    decode_palette, clamp_integer<uint8, 0, Display::PALETTE_COUNT - 1>, encode_palette};
CVar<uint8> backlight_percent{"display.backlight_percent", 50,
    decode_integer<uint8, 0, 100>, clamp_integer<uint8, 0, 100>, encode_integer<uint8>};
CVar<uint32> auto_cut_time_limit_us{"m1.auto_cut_time_limit", 1'000'000,
    decode_seconds, clamp_seconds, encode_seconds, "M1.AutoCutTimeLimit"};
CVar<uint32> simulated_print_time_us{"m1.simulated_print_time", 1'000'000,
    decode_seconds, clamp_seconds, encode_seconds, "M1.SimulatedPrintTime"};
CVar<uint8> rx_sample_delay_us{"m2.rx_sample_delay_us", Gbp::Host::RX_SAMPLE_DELAY_DEFAULT_US,
    decode_integer<uint8, 2, 10>, clamp_integer<uint8, 2, 10>, encode_integer<uint8>};
CVar<PrinterType> printer_type{"m3.printer_type", PrinterType::Mm58,
    decode_printer_type, normalize_printer_type, encode_printer_type};
CVar<PaperType> paper_type{"m3.paper_type", PaperType::Roll,
    decode_paper_type, normalize_paper_type, encode_paper_type};
CVar<PrintProtocol> print_protocol{"m3.protocol", PrintProtocol::EscPos,
    decode_protocol, normalize_protocol, encode_protocol};
CVar<CenteringMode> centering{"m3.centering", CenteringMode::Printer,
    decode_centering, normalize_centering, encode_centering};
CVar<uint16> label_length_mm{"m3.label_length_mm", DEFAULT_LABEL_LENGTH_MM,
    decode_integer<uint16, 1, MAX_LABEL_LENGTH_MM>, clamp_integer<uint16, 1, MAX_LABEL_LENGTH_MM>,
    encode_integer<uint16>};
CVar<uint16> label_gap_mm{"m3.label_gap_mm", 0,
    decode_integer<uint16, 0, MAX_LABEL_GAP_MM>, clamp_integer<uint16, 0, MAX_LABEL_GAP_MM>,
    encode_integer<uint16>};
CVar<std::array<char, 32>> printer_name{"m3.printer_name", DEFAULT_PRINTER_NAME,
    decode_text<32>, valid_text<32>, encode_text<32>};
CVar<std::array<char, 8>> pin{"m3.pin", DEFAULT_PIN,
    decode_text<8>, valid_text<8>, encode_text<8>};

namespace {

CVar<uint16> printable_width_37mm{"m3.advance.printable_width_dots_37mm", 0,
    decode_width<PrinterType::Mm37>, clamp_width<PrinterType::Mm37>, encode_integer<uint16>};
CVar<uint8> image_scale_37mm{"m3.advance.image_scale_37mm", 0,
    decode_scale<PrinterType::Mm37>, normalize_scale<PrinterType::Mm37>, encode_scale};
CVar<uint16> left_margin_37mm_1x{"m3.advance.left_margin_dots_37mm_1x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_37mm_1_5x{"m3.advance.left_margin_dots_37mm_1_5x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> printable_width_58mm{"m3.advance.printable_width_dots_58mm", 0,
    decode_width<PrinterType::Mm58>, clamp_width<PrinterType::Mm58>, encode_integer<uint16>};
CVar<uint8> image_scale_58mm{"m3.advance.image_scale_58mm", 0,
    decode_scale<PrinterType::Mm58>, normalize_scale<PrinterType::Mm58>, encode_scale};
CVar<uint16> left_margin_58mm_1x{"m3.advance.left_margin_dots_58mm_1x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_58mm_1_5x{"m3.advance.left_margin_dots_58mm_1_5x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_58mm_2x{"m3.advance.left_margin_dots_58mm_2x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> printable_width_80mm{"m3.advance.printable_width_dots_80mm", 0,
    decode_width<PrinterType::Mm80>, clamp_width<PrinterType::Mm80>, encode_integer<uint16>};
CVar<uint8> image_scale_80mm{"m3.advance.image_scale_80mm", 0,
    decode_scale<PrinterType::Mm80>, normalize_scale<PrinterType::Mm80>, encode_scale};
CVar<uint16> left_margin_80mm_1x{"m3.advance.left_margin_dots_80mm_1x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_80mm_2x{"m3.advance.left_margin_dots_80mm_2x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_80mm_3x{"m3.advance.left_margin_dots_80mm_3x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> printable_width_110mm{"m3.advance.printable_width_dots_110mm", 0,
    decode_width<PrinterType::Mm110>, clamp_width<PrinterType::Mm110>, encode_integer<uint16>};
CVar<uint8> image_scale_110mm{"m3.advance.image_scale_110mm", 0,
    decode_scale<PrinterType::Mm110>, normalize_scale<PrinterType::Mm110>, encode_scale};
CVar<uint16> left_margin_110mm_1x{"m3.advance.left_margin_dots_110mm_1x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_110mm_2x{"m3.advance.left_margin_dots_110mm_2x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_110mm_3x{"m3.advance.left_margin_dots_110mm_3x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_110mm_4x{"m3.advance.left_margin_dots_110mm_4x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};
CVar<uint16> left_margin_110mm_5x{"m3.advance.left_margin_dots_110mm_5x", AUTO_LEFT_MARGIN,
    decode_integer<uint16, 0, MAX_LEFT_MARGIN_DOTS>, normalize_left_margin, encode_integer<uint16>};

CVar<uint16>* left_margin_slot(const PrinterType type, const uint8 scale_quarters) {
    switch (type) {
        case PrinterType::Mm37:
            if (scale_quarters == SCALE_1X) return &left_margin_37mm_1x;
            if (scale_quarters == SCALE_1_5X) return &left_margin_37mm_1_5x;
            break;
        case PrinterType::Mm58:
            if (scale_quarters == SCALE_1X) return &left_margin_58mm_1x;
            if (scale_quarters == SCALE_1_5X) return &left_margin_58mm_1_5x;
            if (scale_quarters == SCALE_2X) return &left_margin_58mm_2x;
            break;
        case PrinterType::Mm80:
            if (scale_quarters == SCALE_1X) return &left_margin_80mm_1x;
            if (scale_quarters == SCALE_2X) return &left_margin_80mm_2x;
            if (scale_quarters == SCALE_3X) return &left_margin_80mm_3x;
            break;
        case PrinterType::Mm110:
            if (scale_quarters == SCALE_1X) return &left_margin_110mm_1x;
            if (scale_quarters == SCALE_2X) return &left_margin_110mm_2x;
            if (scale_quarters == SCALE_3X) return &left_margin_110mm_3x;
            if (scale_quarters == SCALE_4X) return &left_margin_110mm_4x;
            if (scale_quarters == SCALE_5X) return &left_margin_110mm_5x;
            break;
        case PrinterType::Count: break;
    }
    return nullptr;
}

}  // namespace

M3Config current_m3_config() {
    M3Config config{};
    config.printer_name = printer_name.get();
    config.pin = pin.get();
    config.printer_type = printer_type.get();
    config.paper_type = paper_type.get();
    config.protocol = print_protocol.get();
    config.centering = centering.get();
    config.label_length_mm = label_length_mm.get();
    config.label_gap_mm = label_gap_mm.get();
    config.advance.mm37 = {printable_width_37mm.get(), image_scale_37mm.get()};
    config.advance.mm58 = {printable_width_58mm.get(), image_scale_58mm.get()};
    config.advance.mm80 = {printable_width_80mm.get(), image_scale_80mm.get()};
    config.advance.mm110 = {printable_width_110mm.get(), image_scale_110mm.get()};
    const auto scale_quarters = config.resolved_advance().image_scale_quarters;
    const auto* margin = left_margin_slot(config.printer_type, scale_quarters);
    if (margin != nullptr) {
        switch (config.printer_type) {
            case PrinterType::Mm37: config.advance.mm37.left_margin_dots = margin->get(); break;
            case PrinterType::Mm58: config.advance.mm58.left_margin_dots = margin->get(); break;
            case PrinterType::Mm80: config.advance.mm80.left_margin_dots = margin->get(); break;
            case PrinterType::Mm110: config.advance.mm110.left_margin_dots = margin->get(); break;
            case PrinterType::Count: break;
        }
    }
    return config;
}

bool set_m3_image_scale(const uint8 scale_quarters, const WriteMode mode) {
    const PrinterType type = printer_type.get();
    const uint16 width = current_m3_config().resolved_advance().printable_width_dots;
    if (!printer_type_supports_scale(type, scale_quarters) ||
        scaled_dots(Gbr::Format::GBR_WIDTH, scale_quarters) > width) return false;
    switch (type) {
        case PrinterType::Mm37: return image_scale_37mm.set(scale_quarters, mode);
        case PrinterType::Mm58: return image_scale_58mm.set(scale_quarters, mode);
        case PrinterType::Mm80: return image_scale_80mm.set(scale_quarters, mode);
        case PrinterType::Mm110: return image_scale_110mm.set(scale_quarters, mode);
        case PrinterType::Count: return false;
    }
    return false;
}

bool set_m3_left_margin_dots(const uint16 dots, const WriteMode mode) {
    if (dots > MAX_LEFT_MARGIN_DOTS) return false;
    const auto scale_quarters = current_m3_config().resolved_advance().image_scale_quarters;
    auto* margin = left_margin_slot(printer_type.get(), scale_quarters);
    return margin != nullptr && margin->set(dots, mode);
}

}  // namespace Config::Vars
