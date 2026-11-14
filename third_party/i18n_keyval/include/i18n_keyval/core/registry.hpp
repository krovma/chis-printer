#pragma once

#include <memory>
#include <string>

#include "i18n_keyval/core/translator.hpp"

namespace i18n {

class registry {
  public:
    std::string locale;

    registry() = default;
    registry(const registry&) = delete;
    registry& operator=(const registry&) = delete;

    void initialize_translator(std::shared_ptr<translator> translator);
    [[nodiscard]] std::string translate(const char* key, std::size_t length) noexcept;
    void set_locale(std::string locale) noexcept;
    [[nodiscard]] static registry& instance() noexcept;

  private:
    std::shared_ptr<translator> translator_;
};

}  // namespace i18n
