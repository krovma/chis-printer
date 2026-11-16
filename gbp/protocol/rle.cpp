// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/protocol/rle.h"

#include <algorithm>

namespace gbp::protocol {

void RleDecoder::reset(const bool compressed) {
    compressed_ = compressed;
    state_ = State::Control;
    remaining_ = 0;
}

std::size_t RleDecoder::feed(const std::uint8_t input, std::uint8_t output[129]) {
    if (!compressed_) {
        output[0] = input;
        return 1;
    }

    switch (state_) {
        case State::Control:
            if (input < 0x80) {
                remaining_ = input + 1;
                state_ = State::Literal;
            } else {
                remaining_ = static_cast<std::uint8_t>(input - 0x80 + 2);
                state_ = State::RepeatValue;
            }
            return 0;
        case State::Literal:
            output[0] = input;
            if (--remaining_ == 0) {
                state_ = State::Control;
            }
            return 1;
        case State::RepeatValue:
            std::fill_n(output, remaining_, input);
            state_ = State::Control;
            return remaining_;
    }
    return 0;
}

bool RleDecoder::complete() const {
    return state_ == State::Control;
}

}  // namespace gbp::protocol
