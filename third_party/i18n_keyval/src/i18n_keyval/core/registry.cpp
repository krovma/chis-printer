#include "i18n_keyval/core/registry.hpp"

namespace i18n {

void registry::initialize_translator(std::shared_ptr<translator> translator) {
    translator_ = std::move(translator);
    translator_->set_locale(locale);
}

std::string registry::translate(const char* key, const std::size_t length) noexcept {
    if (translator_ == nullptr) {
        return std::string{key, length};
    }
    return translator_->translate(key, length);
}

void registry::set_locale(std::string locale_value) noexcept {
    locale = std::move(locale_value);
    if (translator_ != nullptr) {
        translator_->set_locale(locale);
    }
}

registry& registry::instance() noexcept {
    static registry instance;
    return instance;
}

}  // namespace i18n
