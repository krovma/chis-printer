// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace storage {

inline bool parse_gbr_filename(const char* name, std::uint64_t& number) {
    const std::size_t length = std::strlen(name);
    if (length < 9 || name[length - 4] != '.' ||
        (name[length - 3] != 'g' && name[length - 3] != 'G') ||
        (name[length - 2] != 'b' && name[length - 2] != 'B') ||
        (name[length - 1] != 'r' && name[length - 1] != 'R')) {
        return false;
    }

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < length - 4; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        const std::uint8_t digit = static_cast<std::uint8_t>(name[i] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    number = value;
    return true;
}

inline bool format_gbr_path(const std::uint64_t number, char* path, const std::size_t capacity) {
    const int length = std::snprintf(path, capacity, "0:/DCIM/%05llu.gbr",
                                     static_cast<unsigned long long>(number));
    return length > 0 && static_cast<std::size_t>(length) < capacity;
}

}  // namespace storage
