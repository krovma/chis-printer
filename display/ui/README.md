# Display UI Text Tools

`text.h/.cpp` provides the reusable text path used by the firmware UI:

- `initialize()` installs the `i18n_keyval` in-memory translator.
- `set_locale()` selects English, Chinese, or Japanese for translated keys.
- `draw_utf8()` validates UTF-8, finds generated 1bpp glyphs, and draws 8 x 16 or 16 x 16 cells.
- `draw_key()` renders any key from `resource/text/display_texts.h`.
- `screen.h/.cpp` draws centered text on the LCD.

The application initializes the translator with English. The `locale` field in
`config.toml` is reserved for later UI localization and is not loaded yet.
`resource/text/display_texts.h` supplies the font generator's glyph set;
regenerate `resource/font/generated_font.h` after changing it.
