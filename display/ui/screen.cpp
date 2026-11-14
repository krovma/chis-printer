#include "display/ui/screen.h"

#include <cstdint>

namespace display::ui {

bool draw_greeting_screen(const Locale requested_locale, const Gray4 background, const Gray4 foreground,
                          const std::int16_t letter_spacing) {
    return draw_message_screen({requested_locale, TextId::Greeting}, background, foreground, letter_spacing);
}

bool draw_message_screen(const MessageSelection selection, const Gray4 background, const Gray4 foreground,
                         const std::int16_t letter_spacing) {
    if (!set_message(selection)) {
        return false;
    }

    const std::int16_t width = measure_utf8(active_text(), letter_spacing);
    if (width < 0 || width > WIDTH) {
        return false;
    }

    display::clear(background);
    const std::int16_t x = static_cast<std::int16_t>((static_cast<std::int32_t>(WIDTH) - width) / 2);
    const std::int16_t y = static_cast<std::int16_t>((static_cast<std::int32_t>(HEIGHT) - 16) / 2);
    return draw_active_text(x, y, foreground, letter_spacing);
}

}  // namespace display::ui
