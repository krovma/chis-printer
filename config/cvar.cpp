// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "config/cvar.h"

#include <climits>

#include "storage/sd_card.h"

namespace Config {

Controller& Controller::instance() {
    static Controller controller;
    return controller;
}

void Controller::register_var(CVarBase& variable) {
    variable.next_ = first_;
    first_ = &variable;
}

void Controller::unregister_var(CVarBase& variable) {
    CVarBase** link = &first_;
    while (*link != nullptr) {
        if (*link == &variable) {
            *link = variable.next_;
            return;
        }
        link = &(*link)->next_;
    }
}

bool Controller::reload() {
    std::string_view contents;
    return Storage::read_config_text(contents) && load_text(contents);
}

bool Controller::load_text(const std::string_view contents) {
    if (contents.size() > INT_MAX) return false;
    const toml_result_t document = toml_parse(contents.data(), static_cast<int>(contents.size()));
    if (!document.ok) {
        toml_free(document);
        return false;
    }
    const toml_datum_t version = toml_get(document.toptab, "format_version");
    if (version.type != TOML_INT64 || version.u.int64 != 1) {
        toml_free(document);
        return false;
    }
    for (CVarBase* variable = first_; variable != nullptr; variable = variable->next_) {
        variable->load(document.toptab);
    }
    toml_free(document);
    return true;
}

void Controller::reset_defaults() {
    for (CVarBase* variable = first_; variable != nullptr; variable = variable->next_) {
        variable->reset();
    }
}

const CVarBase* Controller::find(const std::string_view key) const {
    for (const CVarBase* variable = first_; variable != nullptr; variable = variable->next_) {
        if (key == variable->key()) return variable;
    }
    return nullptr;
}

usize Controller::registered_count() const {
    usize count = 0;
    for (const CVarBase* variable = first_; variable != nullptr; variable = variable->next_) ++count;
    return count;
}

bool Controller::persist(const std::string_view key, const std::string_view toml_value) {
    return Storage::write_config_value(key, toml_value);
}

CVarBase::CVarBase(const char* key, const char* legacy_key)
    : legacy_key_(legacy_key), key_(key) {
    Controller::instance().register_var(*this);
}

CVarBase::~CVarBase() { Controller::instance().unregister_var(*this); }

}  // namespace Config
