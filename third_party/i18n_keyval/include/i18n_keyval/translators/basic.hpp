#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "i18n_keyval/core/common.hpp"

namespace i18n {

using translation_table = std::unordered_map<std::string, std::string>;
using translations = std::unordered_map<std::string, translation_table>;

namespace translators {

class basic {
  public:
    explicit basic(translations translations) : translations_(std::move(translations)) {}

    void set_locale(const std::string& locale) {
        const auto it = translations_.find(locale);
        if (it == translations_.end()) {
            throw_i18n_exception("Locale not found");
            return;
        }
        values_ = &it->second;
    }

    [[nodiscard]] std::string translate(const char* key, std::size_t length) const noexcept {
        const std::string_view view{key, length};
        if (values_ == nullptr) {
            return std::string{view};
        }

        const auto it = values_->find(std::string{view});
        if (it == values_->end()) {
            return std::string{view};
        }
        return it->second;
    }

  private:
    translations translations_;
    translation_table* values_ = nullptr;
};

}  // namespace translators
}  // namespace i18n
