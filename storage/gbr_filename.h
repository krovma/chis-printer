// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <cstdio>
#include <cstring>
#include <limits>

#include "types.h"

namespace Storage {

inline bool parse_gbr_filename(const char* name, uint64& number) {
    const usize length = std::strlen(name);
    if (length < 9 || name[length - 4] != '.' || (name[length - 3] != 'g' && name[length - 3] != 'G') ||
        (name[length - 2] != 'b' && name[length - 2] != 'B') || (name[length - 1] != 'r' && name[length - 1] != 'R')) {
        return false;
    }

    uint64 value = 0;
    for (usize i = 0; i < length - 4; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        const uint8 digit = static_cast<uint8>(name[i] - '0');
        if (value > (std::numeric_limits<uint64>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    number = value;
    return true;
}

inline bool format_gbr_path(const uint64 number, char* path, const usize capacity) {
    const int length = std::snprintf(path, capacity, "0:/DCIM/%05llu.gbr",
                                     static_cast<unsigned long long>(number));
    return length > 0 && static_cast<usize>(length) < capacity;
}

}  // namespace Storage
