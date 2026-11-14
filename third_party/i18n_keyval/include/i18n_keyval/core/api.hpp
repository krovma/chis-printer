#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "i18n_keyval/core/registry.hpp"
#include "i18n_keyval/translators/basic.hpp"

namespace i18n {

[[nodiscard]] std::string t(std::string_view key) noexcept;
[[nodiscard]] std::string t(std::string_view key, std::size_t count) noexcept;

template <typename T = translators::basic, typename... Args>
void initialize_translator(Args&&... args) {
    auto translator = std::make_shared<i18n::translator>(T{std::forward<Args>(args)...});
    registry::instance().initialize_translator(std::move(translator));
}

std::string locale() noexcept;
void set_locale(std::string locale);

}  // namespace i18n

namespace i18n::literals {

[[nodiscard]] inline std::string operator""_t(const char* key, std::size_t length) noexcept {
    return i18n::registry::instance().translate(key, length);
}

}  // namespace i18n::literals
