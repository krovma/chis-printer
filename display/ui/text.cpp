// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "display/ui/text.h"

#include <string>

#include "types.h"
#include "i18n_keyval/i18n.hpp"
#include "i18n_keyval/translators/basic.hpp"
#include "resource/font/generated_font.h"
#include "resource/text/display_texts.h"

namespace Display::Ui {
namespace {

constexpr std::string_view LOCALE_ENGLISH  = "en";
constexpr std::string_view LOCALE_CHINESE  = "zh";
constexpr std::string_view LOCALE_JAPANESE = "ja";

bool translator_initialized = false;
Locale current_locale_value = DEFAULT_LOCALE;

std::string_view locale_name(const Locale locale) {
    switch (locale) {
        case Locale::English: return LOCALE_ENGLISH;
        case Locale::Chinese: return LOCALE_CHINESE;
        case Locale::Japanese: return LOCALE_JAPANESE;
        default: return LOCALE_ENGLISH;
    }
}

i18n::translations make_translations() {
    i18n::translations translations;
    for (const auto &entry : FirmwareText::DISPLAY_TEXTS) {
        translations[std::string{entry.locale}][std::string{entry.key}] = std::string{entry.value};
    }
    return translations;
}

bool decode_utf8(const std::string_view text, usize& offset, uint32& codepoint) noexcept {
    if (offset >= text.size()) {
        return false;
    }

    const auto byte = [&text](const usize index) {
        return static_cast<uint8>(text[index]);
    };
    const uint8 first = byte(offset);
    uint32 value = 0;
    usize length = 0;
    uint32 minimum = 0;

    if (first <= 0x7F) {
        value   = first;
        length  = 1;
        minimum = 0;
    } else if (first >= 0xC2 && first <= 0xDF) {
        value   = first & 0x1F;
        length  = 2;
        minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        value   = first & 0x0F;
        length  = 3;
        minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        value   = first & 0x07;
        length  = 4;
        minimum = 0x10000;
    } else {
        return false;
    }

    if (offset + length > text.size()) {
        return false;
    }
    for (usize index = 1; index < length; ++index) {
        const uint8 continuation = byte(offset + index);
        if ((continuation & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | (continuation & 0x3F);
    }

    // Reject overlong forms, surrogate code points, and values beyond Unicode.
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }

    codepoint = value;
    offset += length;
    return true;
}

bool is_valid_locale(const Locale locale) {
    return locale == Locale::English || locale == Locale::Chinese || locale == Locale::Japanese;
}

bool measure_utf8_impl(const std::string_view utf8, const int16 letter_spacing, int32& width) {
    usize offset = 0;
    width = 0;
    bool first_glyph = true;

    while (offset < utf8.size()) {
        uint32 codepoint = 0;
        if (!decode_utf8(utf8, offset, codepoint)) {
            return false;
        }

        const FirmwareFont::Glyph* glyph = FirmwareFont::find_glyph(codepoint);
        if (glyph == nullptr) {
            return false;
        }

        if (!first_glyph) {
            width += letter_spacing;
        }
        width += glyph->width;
        first_glyph = false;
    }

    return width >= 0 && width <= 0x7FFF;
}

void draw_glyph(const FirmwareFont::Glyph& glyph, const int32 x, const int32 y, const Gray4 gray) {
    for (int32 row = 0; row < 16; ++row) {
        for (int32 column = 0; column < glyph.width; ++column) {
            const uint8 source = glyph.rows[row * glyph.row_stride + column / 8];
            if ((source & (0x80u >> (column & 7))) == 0) {
                continue;
            }

            const int32 pixel_x = x + column;
            const int32 pixel_y = y + row;
            if (pixel_x >= 0 && pixel_x < WIDTH && pixel_y >= 0 && pixel_y < HEIGHT) {
                Display::set_pixel(static_cast<uint16>(pixel_x), static_cast<uint16>(pixel_y), gray);
            }
        }
    }
}

}  // namespace

bool initialize(const Locale initial_locale) {
    if (!is_valid_locale(initial_locale)) {
        return false;
    }

    if (translator_initialized) {
        return set_locale(initial_locale);
    }

    // Set the registry locale before constructing the translator so the
    // library does not briefly try to select its empty default locale.
    i18n::set_locale(std::string{locale_name(initial_locale)});
    i18n::initialize_translator<i18n::translators::basic>(make_translations());
    translator_initialized = true;
    current_locale_value = initial_locale;
    return true;
}

bool set_locale(const Locale requested_locale) {
    if (!is_valid_locale(requested_locale)) {
        return false;
    }

    i18n::set_locale(std::string{locale_name(requested_locale)});
    current_locale_value = requested_locale;
    return true;
}

Locale locale() noexcept {
    return current_locale_value;
}

std::string translate(const std::string_view key) {
    return i18n::t(key);
}

int16 measure_utf8(const std::string_view utf8, const int16 letter_spacing) {
    int32 width = 0;
    if (!measure_utf8_impl(utf8, letter_spacing, width)) {
        return -1;
    }
    return static_cast<int16>(width);
}

bool draw_utf8(const std::string_view utf8, const int16 x, const int16 y, const Gray4 gray,
               const int16 letter_spacing) {
    int32 width = 0;
    if (!measure_utf8_impl(utf8, letter_spacing, width)) {
        return false;
    }

    usize offset = 0;
    int32 cursor = x;
    bool first_glyph = true;
    while (offset < utf8.size()) {
        uint32 codepoint = 0;
        if (!decode_utf8(utf8, offset, codepoint)) {
            return false;
        }

        const FirmwareFont::Glyph* glyph = FirmwareFont::find_glyph(codepoint);
        if (glyph == nullptr) {
            return false;
        }
        if (!first_glyph) {
            cursor += letter_spacing;
        }
        draw_glyph(*glyph, cursor, y, gray);
        cursor += glyph->width;
        first_glyph = false;
    }
    return true;
}

bool draw_key(const std::string_view key, const int16 x, const int16 y, const Gray4 gray,
              const int16 letter_spacing) {
    const std::string text = translate(key);
    return draw_utf8(text, x, y, gray, letter_spacing);
}

}  // namespace Display::Ui
