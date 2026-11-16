// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "display/ui/text.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "i18n_keyval/i18n.hpp"
#include "i18n_keyval/translators/basic.hpp"
#include "resource/font/generated_font.h"
#include "resource/text/display_texts.h"

namespace display::ui {
namespace {

constexpr std::string_view LOCALE_ENGLISH  = "en";
constexpr std::string_view LOCALE_CHINESE  = "zh";
constexpr std::string_view LOCALE_JAPANESE = "ja";
constexpr std::string_view GREETING_KEY    = "greeting";

bool translator_initialized = false;
Locale current_locale_value = DEFAULT_LOCALE;
TextId current_text_value   = DEFAULT_TEXT;
std::string current_greeting_value;
std::string current_text_value_cache;

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
    for (const auto& entry : firmware_text::DISPLAY_TEXTS) {
        translations[std::string{entry.locale}][std::string{entry.key}] = std::string{entry.value};
    }
    return translations;
}

bool decode_utf8(const std::string_view text, std::size_t& offset, std::uint32_t& codepoint) noexcept {
    if (offset >= text.size()) {
        return false;
    }

    const auto byte = [&text](const std::size_t index) {
        return static_cast<std::uint8_t>(text[index]);
    };
    const std::uint8_t first = byte(offset);
    std::uint32_t value      = 0;
    std::size_t length       = 0;
    std::uint32_t minimum    = 0;

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
    for (std::size_t index = 1; index < length; ++index) {
        const std::uint8_t continuation = byte(offset + index);
        if ((continuation & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | (continuation & 0x3F);
    }

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

bool is_valid_text(const TextId text) {
    return text == TextId::Greeting || text == TextId::Printer;
}

void refresh_cached_text() {
    current_greeting_value = i18n::t(GREETING_KEY);
    current_text_value_cache = i18n::t(text_key(current_text_value));
}

bool measure_utf8_impl(const std::string_view utf8, const std::int16_t letter_spacing, std::int32_t& width) {
    std::size_t offset = 0;
    width              = 0;
    bool first_glyph    = true;

    while (offset < utf8.size()) {
        std::uint32_t codepoint = 0;
        if (!decode_utf8(utf8, offset, codepoint)) {
            return false;
        }

        const firmware_font::Glyph* glyph = firmware_font::find_glyph(codepoint);
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

void draw_glyph(const firmware_font::Glyph& glyph, const std::int32_t x, const std::int32_t y, const Gray4 gray) {
    for (std::int32_t row = 0; row < 16; ++row) {
        for (std::int32_t column = 0; column < glyph.width; ++column) {
            const std::uint8_t source = glyph.rows[row * glyph.row_stride + column / 8];
            if ((source & (0x80u >> (column & 7))) == 0) {
                continue;
            }

            const std::int32_t pixel_x = x + column;
            const std::int32_t pixel_y = y + row;
            if (pixel_x >= 0 && pixel_x < WIDTH && pixel_y >= 0 && pixel_y < HEIGHT) {
                display::set_pixel(static_cast<std::uint16_t>(pixel_x), static_cast<std::uint16_t>(pixel_y), gray);
            }
        }
    }
}

}  // namespace

bool initialize(const Locale initial_locale, const TextId initial_text) {
    if (!is_valid_locale(initial_locale) || !is_valid_text(initial_text)) {
        return false;
    }

    if (translator_initialized) {
        return set_message({initial_locale, initial_text});
    }

    // Set the registry locale before constructing the translator so the
    // library does not briefly try to select its empty default locale.
    i18n::set_locale(std::string{locale_name(initial_locale)});
    i18n::initialize_translator<i18n::translators::basic>(make_translations());
    translator_initialized = true;
    current_locale_value = initial_locale;
    current_text_value   = initial_text;
    refresh_cached_text();
    return true;
}

bool set_locale(const Locale requested_locale) {
    if (!is_valid_locale(requested_locale)) {
        return false;
    }

    i18n::set_locale(std::string{locale_name(requested_locale)});
    current_locale_value = requested_locale;
    if (translator_initialized) {
        refresh_cached_text();
    }
    return true;
}

Locale locale() noexcept {
    return current_locale_value;
}

bool set_message(const MessageSelection selection) {
    if (!is_valid_locale(selection.locale) || !is_valid_text(selection.text)) {
        return false;
    }

    current_text_value = selection.text;
    return set_locale(selection.locale);
}

MessageSelection message() noexcept {
    return {current_locale_value, current_text_value};
}

TextId text_id() noexcept {
    return current_text_value;
}

std::string translate(const std::string_view key) {
    return i18n::t(key);
}

std::string_view greeting() noexcept {
    return current_greeting_value;
}

std::string_view active_text() noexcept {
    return current_text_value_cache;
}

std::int16_t measure_utf8(const std::string_view utf8, const std::int16_t letter_spacing) {
    std::int32_t width = 0;
    if (!measure_utf8_impl(utf8, letter_spacing, width)) {
        return -1;
    }
    return static_cast<std::int16_t>(width);
}

bool draw_utf8(const std::string_view utf8, const std::int16_t x, const std::int16_t y, const Gray4 gray,
               const std::int16_t letter_spacing) {
    std::int32_t width = 0;
    if (!measure_utf8_impl(utf8, letter_spacing, width)) {
        return false;
    }

    std::size_t offset = 0;
    std::int32_t cursor = x;
    bool first_glyph = true;
    while (offset < utf8.size()) {
        std::uint32_t codepoint = 0;
        if (!decode_utf8(utf8, offset, codepoint)) {
            return false;
        }

        const firmware_font::Glyph* glyph = firmware_font::find_glyph(codepoint);
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

bool draw_key(const std::string_view key, const std::int16_t x, const std::int16_t y, const Gray4 gray,
              const std::int16_t letter_spacing) {
    const std::string text = translate(key);
    return draw_utf8(text, x, y, gray, letter_spacing);
}

bool draw_greeting(const std::int16_t x, const std::int16_t y, const Gray4 gray, const std::int16_t letter_spacing) {
    return draw_utf8(greeting(), x, y, gray, letter_spacing);
}

bool draw_active_text(const std::int16_t x, const std::int16_t y, const Gray4 gray,
                      const std::int16_t letter_spacing) {
    return draw_utf8(active_text(), x, y, gray, letter_spacing);
}

}  // namespace display::ui
