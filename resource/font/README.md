# Display Font Generation

`../text/display_texts.h` is the single source for every string that may be
drawn on the LCD. Log messages are intentionally outside that table.

The checked-in `generated_font.h` contains only the codepoints used by the
current display text table. It stores one-bit, MSB-first rows in
16-pixel cells: ASCII characters use 8 x 16 cells and non-ASCII characters
use 16 x 16 cells.

Regenerate the header after changing the display text table:

```text
python -m pip install -r resource/font/requirements.txt
python resource/font/generate_font_header.py
```

The script validates that every UTF-8 codepoint in the text table exists in
`resource/ZhengGeDianHei-16.ttf` before writing the header.
