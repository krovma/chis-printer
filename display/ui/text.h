// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <string>
#include <string_view>

#include "types.h"
#include "display/zjy154s0800.h"

namespace Display::Ui {

enum class Locale : uint8 {
    English,
    Chinese,
    Japanese,
};

inline constexpr Locale DEFAULT_LOCALE = Locale::English;

constexpr Locale next_locale(const Locale locale) noexcept {
    switch (locale) {
        case Locale::English: return Locale::Chinese;
        case Locale::Chinese: return Locale::Japanese;
        case Locale::Japanese: return Locale::English;
        default: return DEFAULT_LOCALE;
    }
}

// Build the in-memory i18n_keyval translator from resource/text/display_texts.h.
bool initialize(Locale initial_locale = DEFAULT_LOCALE);
bool set_locale(Locale locale);
Locale locale() noexcept;

// Return a translated display string. The returned string owns its UTF-8 data.
std::string translate(std::string_view key);

// Return the pixel width, or -1 when UTF-8 is malformed or a glyph is absent.
int16 measure_utf8(std::string_view utf8, int16 letter_spacing = 0);

// Decode UTF-8, map each codepoint to the generated 1bpp glyph and draw it.
// Returns false when the input is malformed UTF-8 or a glyph is unavailable.
bool draw_utf8(std::string_view utf8, int16 x, int16 y, Gray4 gray, int16 letter_spacing = 0);
bool draw_key(std::string_view key, int16 x, int16 y, Gray4 gray, int16 letter_spacing = 0);

}  // namespace Display::Ui
