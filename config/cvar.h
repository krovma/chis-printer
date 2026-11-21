// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <string_view>

#include "types.h"
#include "tomlc17.h"

namespace Config {

enum class WriteMode : uint8 { RuntimeOnly, RuntimeAndToml };

class CVarBase;

class Controller {
  public:
    static Controller& instance();

    // A failed parse or read leaves all runtime values unchanged.
    bool reload();
    bool load_text(std::string_view contents);
    void reset_defaults();
    const CVarBase* find(std::string_view key) const;
    usize registered_count() const;

  private:
    Controller() = default;
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    friend class CVarBase;
    template <typename T> friend class CVar;
    void register_var(CVarBase& variable);
    void unregister_var(CVarBase& variable);
    bool persist(std::string_view key, std::string_view toml_value);

    CVarBase* first_ = nullptr;
};

class CVarBase {
  public:
    CVarBase(const char* key, const char* legacy_key = nullptr);
    virtual ~CVarBase();
    CVarBase(const CVarBase&) = delete;
    CVarBase& operator=(const CVarBase&) = delete;

    const char* key() const { return key_; }

  protected:
    const char* legacy_key_;

  private:
    friend class Controller;
    virtual void load(toml_datum_t root) = 0;
    virtual void reset() = 0;

    const char* key_;
    CVarBase* next_ = nullptr;
};

template <typename T>
class CVar final : public CVarBase {
  public:
    // Define CVars at global scope with static key strings. Construction registers
    // them with Controller; Decode/Normalize/Encode own each value's policy.
    using Decode = bool (*)(toml_datum_t, T&);
    using Normalize = bool (*)(T&);
    using Encode = bool (*)(const T&, char*, usize);

    CVar(const char* key, T initial, Decode decode, Normalize normalize, Encode encode,
         const char* legacy_key = nullptr)
        : CVarBase(key, legacy_key), initial_(initial), value_(initial), decode_(decode),
          normalize_(normalize), encode_(encode) {}

    const T& get() const { return value_; }

    // Validation may clamp a candidate. Persistence commits before changing memory.
    bool set(T candidate, WriteMode mode = WriteMode::RuntimeOnly) {
        if (mode != WriteMode::RuntimeOnly && mode != WriteMode::RuntimeAndToml) return false;
        if (!normalize_(candidate)) return false;
        if (mode == WriteMode::RuntimeAndToml) {
            char encoded[128]{};
            if (!encode_(candidate, encoded, sizeof(encoded)) ||
                !Controller::instance().persist(key(), encoded)) return false;
        }
        value_ = candidate;
        return true;
    }

  private:
    void load(toml_datum_t root) override {
        toml_datum_t datum = toml_seek(root, key());
        if (datum.type == TOML_UNKNOWN && legacy_key_ != nullptr) {
            datum = toml_seek(root, legacy_key_);
        }
        T candidate = initial_;
        if (datum.type != TOML_UNKNOWN && (!decode_(datum, candidate) || !normalize_(candidate))) {
            candidate = initial_;
        }
        value_ = candidate;
    }

    void reset() override { value_ = initial_; }

    const T initial_;
    T value_;
    Decode decode_;
    Normalize normalize_;
    Encode encode_;
};

}  // namespace Config
