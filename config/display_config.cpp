#include "config/display_config.h"

#include <array>
#include <climits>
#include <cstddef>
#include <string_view>

#include "tomlc17.h"

namespace config {
namespace {

constexpr std::array<std::string_view, display::PALETTE_COUNT> PALETTE_NAMES{
    "red", "yellow", "green", "cyan", "blue", "magenta", "white",
};

}  // namespace

bool parse_display_config(const std::string_view contents, DisplayConfig& output) {
    if (contents.size() > INT_MAX) {
        return false;
    }

    const toml_result_t document = toml_parse(contents.data(), static_cast<int>(contents.size()));
    if (!document.ok) {
        toml_free(document);
        return false;
    }

    const toml_datum_t version = toml_get(document.toptab, "format_version");
    const toml_datum_t palette = toml_seek(document.toptab, "display.palette");
    const toml_datum_t backlight = toml_seek(document.toptab, "display.backlight_percent");

    bool valid = version.type == TOML_INT64 && version.u.int64 == 1 &&
                 palette.type == TOML_STRING && backlight.type == TOML_INT64 &&
                 backlight.u.int64 >= 0 && backlight.u.int64 <= 100;
    DisplayConfig parsed;
    if (valid) {
        const std::string_view palette_name{palette.u.str.ptr, static_cast<std::size_t>(palette.u.str.len)};
        valid = false;
        for (std::size_t index = 0; index < PALETTE_NAMES.size(); ++index) {
            if (palette_name == PALETTE_NAMES[index]) {
                parsed.palette_key = static_cast<std::uint8_t>(index);
                parsed.backlight_percent = static_cast<std::uint8_t>(backlight.u.int64);
                valid = true;
                break;
            }
        }
    }

    toml_free(document);
    if (valid) {
        output = parsed;
    }
    return valid;
}

}  // namespace config
