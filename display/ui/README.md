# Display UI Text Tools

`text.h/.cpp` provides the reusable text path used by the firmware UI:

- `initialize()` installs the `i18n_keyval` in-memory translator.
- `set_locale()` selects English, Chinese, or Japanese and refreshes the cached text.
- `set_message()` selects one of the localized message IDs; `next_message()` exposes the configured six-entry cycle.
- `draw_utf8()` validates UTF-8, finds generated 1bpp glyphs, and draws 8 x 16 or 16 x 16 cells.
- `draw_key()` renders any key from `resource/text/display_texts.h`.
- `screen.h/.cpp` provides the current centered message screen used by the startup loop.

The application keeps the current `Locale` value in `main.cpp`. That value is
intended to be replaced by the persisted configuration once config storage is
available. `resource/text/display_texts.h` remains the only source for strings
that can be rendered on the LCD; regenerate `resource/font/generated_font.h`
after changing it.
