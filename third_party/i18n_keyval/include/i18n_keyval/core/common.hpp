#pragma once

#include <cstdio>
#include <exception>

#ifndef I18N_EXCEPTIONS
#define I18N_TRY
#define I18N_CATCH
#define I18N_THROW(exception) std::printf("i18n error: %s\n", (exception).what())
#else
#define I18N_TRY try
#define I18N_CATCH catch (const std::exception&) {}
#define I18N_THROW(exception) throw (exception)
#endif

namespace i18n {

class i18n_exception : public std::exception {
  public:
    explicit i18n_exception(const char* message) : message_(message) {}
    [[nodiscard]] const char* what() const noexcept override {
        return message_;
    }

  private:
    const char* message_;
};

void throw_i18n_exception(const char* message);

}  // namespace i18n
