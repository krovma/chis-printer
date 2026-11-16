#pragma once

#include <array>
#include <string_view>

namespace firmware_text {

struct Translation {
    std::string_view locale;
    std::string_view key;
    std::string_view value;
};

// Every string that may be rendered on the LCD belongs in this table. The
// font generator reads the value column to keep the glyph set synchronized.
inline constexpr std::array<Translation, 9> DISPLAY_TEXTS{{
    {"en", "greeting", u8"Hello World!"},
    {"zh", "greeting", u8"你好，世界！"},
    {"ja", "greeting", u8"よこそう、世界へ！"},
    {"en", "printer", u8"Chis Printer"},
    {"zh", "printer", u8"起司打印机"},
    {"ja", "printer", u8"チーズプリンター"},
    {"en", "sd_card_error", u8"SD CARD ERROR"},
    {"en", "m1_waiting", u8"Waiting"},
    {"en", "m1_receiving", u8"Receiving 0123456789"},
}};

}  // namespace firmware_text
