// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "display/zjy154s0800.h"

namespace display::ui {

enum class Locale : std::uint8_t {
    English,
    Chinese,
    Japanese,
};

enum class TextId : std::uint8_t {
    Greeting,
    Printer,
};

struct MessageSelection {
    Locale locale;
    TextId text;
};

inline constexpr Locale DEFAULT_LOCALE = Locale::English;
inline constexpr TextId DEFAULT_TEXT   = TextId::Greeting;

inline constexpr std::array<MessageSelection, 6> MESSAGE_CYCLE{{
    {Locale::English, TextId::Greeting},
    {Locale::Chinese, TextId::Greeting},
    {Locale::Japanese, TextId::Greeting},
    {Locale::English, TextId::Printer},
    {Locale::Chinese, TextId::Printer},
    {Locale::Japanese, TextId::Printer},
}};

inline constexpr std::size_t MESSAGE_COUNT = MESSAGE_CYCLE.size();

// The application owns the current Locale value so it can later be replaced
// with a persisted configuration value.
constexpr Locale next_locale(const Locale locale) noexcept {
    switch (locale) {
        case Locale::English: return Locale::Chinese;
        case Locale::Chinese: return Locale::Japanese;
        case Locale::Japanese: return Locale::English;
        default: return DEFAULT_LOCALE;
    }
}

constexpr std::string_view text_key(const TextId text) noexcept {
    switch (text) {
        case TextId::Greeting: return "greeting";
        case TextId::Printer: return "printer";
        default: return "greeting";
    }
}

constexpr std::size_t message_index(const MessageSelection selection) noexcept {
    for (std::size_t index = 0; index < MESSAGE_COUNT; ++index) {
        if (MESSAGE_CYCLE[index].locale == selection.locale && MESSAGE_CYCLE[index].text == selection.text) {
            return index;
        }
    }
    return 0;
}

constexpr MessageSelection next_message(const MessageSelection selection) noexcept {
    return MESSAGE_CYCLE[(message_index(selection) + 1) % MESSAGE_COUNT];
}

// Build the in-memory i18n_keyval translator from resource/text/display_texts.h.
bool initialize(Locale initial_locale = DEFAULT_LOCALE, TextId initial_text = DEFAULT_TEXT);
bool set_locale(Locale locale);
Locale locale() noexcept;
bool set_message(MessageSelection selection);
MessageSelection message() noexcept;
TextId text_id() noexcept;

// Return a translated display string. The returned string owns its UTF-8 data.
std::string translate(std::string_view key);

// The active strings are cached and remain valid until the selection changes.
std::string_view greeting() noexcept;
std::string_view active_text() noexcept;

// Return the pixel width, or -1 when UTF-8 is malformed or a glyph is absent.
std::int16_t measure_utf8(std::string_view utf8, std::int16_t letter_spacing = 0);

// Decode UTF-8, map each codepoint to the generated 1bpp glyph and draw it.
// Returns false when the input is malformed UTF-8 or a glyph is unavailable.
bool draw_utf8(std::string_view utf8, std::int16_t x, std::int16_t y, Gray4 gray, std::int16_t letter_spacing = 0);
bool draw_key(std::string_view key, std::int16_t x, std::int16_t y, Gray4 gray, std::int16_t letter_spacing = 0);
bool draw_greeting(std::int16_t x, std::int16_t y, Gray4 gray, std::int16_t letter_spacing = 0);
bool draw_active_text(std::int16_t x, std::int16_t y, Gray4 gray, std::int16_t letter_spacing = 0);

}  // namespace display::ui
