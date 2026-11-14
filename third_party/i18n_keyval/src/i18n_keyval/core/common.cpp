#include "i18n_keyval/core/common.hpp"

namespace i18n {

void throw_i18n_exception(const char* message) {
    I18N_THROW(i18n_exception{message});
}

}  // namespace i18n
