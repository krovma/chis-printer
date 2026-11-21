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
    if (value == "58mm") output = PrinterType::Mm58;
    else if (value == "80mm") output = PrinterType::Mm80;
    else return false;
    return true;
}

bool normalize_printer_type(PrinterType& value) {
    return value == PrinterType::Mm58 || value == PrinterType::Mm80;
}

bool encode_printer_type(const PrinterType& value, char* output, const usize capacity) {
    if (value != PrinterType::Mm58 && value != PrinterType::Mm80) return false;
    const int length = std::snprintf(output, capacity, "\"%s\"",
                                     value == PrinterType::Mm80 ? "80mm" : "58mm");
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

template <uint16 Minimum>
bool decode_width(const toml_datum_t datum, uint16& output) {
    return decode_integer<uint16, Minimum, 1023>(datum, output);
}

template <uint16 Minimum>
bool clamp_width(uint16& value) {
    if (value == 0) return true;
    return clamp_integer<uint16, Minimum, 1023>(value);
}

template <uint8 Expected>
bool decode_scale(const toml_datum_t datum, uint8& output) {
    if (datum.type != TOML_INT64 || datum.u.int64 != Expected) return false;
    output = Expected;
    return true;
}

template <uint8 Expected>
bool normalize_scale(uint8& value) {
    return value == 0 || value == Expected;
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
CVar<std::array<char, 32>> printer_name{"m3.printer_name", DEFAULT_PRINTER_NAME,
    decode_text<32>, valid_text<32>, encode_text<32>};
CVar<std::array<char, 8>> pin{"m3.pin", DEFAULT_PIN,
    decode_text<8>, valid_text<8>, encode_text<8>};
CVar<uint16> printable_width_58mm{"m3.advance.printable_width_dots_58mm", 0,
    decode_width<320>, clamp_width<320>, encode_integer<uint16>};
CVar<uint8> image_scale_58mm{"m3.advance.image_scale_58mm", 0,
    decode_scale<2>, normalize_scale<2>, encode_integer<uint8>};
CVar<uint16> printable_width_80mm{"m3.advance.printable_width_dots_80mm", 0,
    decode_width<480>, clamp_width<480>, encode_integer<uint16>};
CVar<uint8> image_scale_80mm{"m3.advance.image_scale_80mm", 0,
    decode_scale<3>, normalize_scale<3>, encode_integer<uint8>};

M3Config current_m3_config() {
    M3Config config{};
    config.printer_name = printer_name.get();
    config.pin = pin.get();
    config.printer_type = printer_type.get();
    config.advance.mm58 = {printable_width_58mm.get(), image_scale_58mm.get()};
    config.advance.mm80 = {printable_width_80mm.get(), image_scale_80mm.get()};
    return config;
}

}  // namespace Config::Vars
