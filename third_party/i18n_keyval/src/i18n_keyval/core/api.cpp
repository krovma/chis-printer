#include "i18n_keyval/core/api.hpp"

namespace {

std::string make_plural_key(const std::string_view key) noexcept {
    return std::string{key} + "_plural";
}

}  // namespace

namespace i18n {

std::string t(const std::string_view key) noexcept {
    return registry::instance().translate(key.data(), key.size());
}

std::string t(const std::string_view key, const std::size_t count) noexcept {
    std::string translation;
    if (count == 1) {
        translation = registry::instance().translate(key.data(), key.size());
    } else {
        const auto plural_key = make_plural_key(key);
        translation = registry::instance().translate(plural_key.c_str(), plural_key.size());
    }

    const auto position = translation.find("{count}");
    if (position == std::string::npos) {
        return translation;
    }
    return translation.replace(position, 7, std::to_string(count));
}

void set_locale(std::string locale) {
    registry::instance().set_locale(std::move(locale));
}

std::string locale() noexcept {
    return registry::instance().locale;
}

}  // namespace i18n
