#pragma once

#include <memory>
#include <string>
#include <utility>

namespace i18n {

class translator {
  public:
    template <typename T>
    explicit translator(T&& translator)
        : translator_(std::make_shared<model<T>>(std::forward<T>(translator))) {}

    void set_locale(const std::string& locale) {
        translator_->set_locale(locale);
    }

    std::string translate(const char* key, std::size_t length) {
        return translator_->translate(key, length);
    }

    struct base {
        virtual ~base() = default;
        virtual void set_locale(const std::string& locale) = 0;
        virtual std::string translate(const char* key, std::size_t length) const = 0;
    };

    template <typename T>
    struct model final : base {
        explicit model(T object) : object_(std::move(object)) {}

        void set_locale(const std::string& locale) override {
            object_.set_locale(locale);
        }

        std::string translate(const char* key, std::size_t length) const override {
            return object_.translate(key, length);
        }

      private:
        T object_;
    };

  private:
    std::shared_ptr<base> translator_;
};

}  // namespace i18n
