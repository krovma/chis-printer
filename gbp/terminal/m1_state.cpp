// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/m1_state.h"

#include <cstdio>

namespace gbp::terminal {
namespace {

constexpr std::uint32_t VIEW_MASK = 0x03;
constexpr std::uint32_t MAX_GENERATION = 0x3FFFFFFF;

std::uint32_t pack_view(const ViewState view, const std::uint32_t generation) {
    return (generation << 2) | static_cast<std::uint32_t>(view);
}

}  // namespace

M1State::M1State() : receiver_(buffers_[0]) {}

M1State::Snapshot M1State::snapshot() const {
    const std::uint32_t word = view_word_.load(std::memory_order_acquire);
    return {static_cast<ViewState>(word & VIEW_MASK), frames_.load(std::memory_order_acquire), word >> 2};
}

void M1State::core1_on_receiver_change() {
    if (receiver_.state() == ViewState::Preview) {
        if (slot_.load(std::memory_order_acquire) != Slot::Empty) {
            printf("ERROR: M1 image discarded: previous frame still owned by Core0\n");
            receiver_.reset();
            frames_.store(0, std::memory_order_release);
            view_word_.store(pack_view(ViewState::Waiting, generation_), std::memory_order_release);
            return;
        }

        const std::uint32_t next_generation = generation_ == MAX_GENERATION ? 1 : generation_ + 1;
        handed_off_ = {receiver_.image_data(), receiver_.image_size(), receiver_.palette(), next_generation};
        active_buffer_ ^= 1u;
        receiver_.use_buffer(buffers_[active_buffer_]);
        frames_.store(0, std::memory_order_release);
        generation_ = next_generation;
        view_word_.store(pack_view(ViewState::Preview, generation_), std::memory_order_release);
        slot_.store(Slot::Ready, std::memory_order_release);
        return;
    }

    frames_.store(receiver_.data_frames(), std::memory_order_release);
    view_word_.store(pack_view(receiver_.state(), generation_), std::memory_order_release);
}

void M1State::core1_process_confirmation() {
    const std::uint32_t requested_generation = confirmation_generation_.exchange(0, std::memory_order_acq_rel);
    const std::uint32_t word = view_word_.load(std::memory_order_relaxed);
    if (requested_generation != 0 && requested_generation == (word >> 2) &&
        static_cast<ViewState>(word & VIEW_MASK) == ViewState::Preview) {
        view_word_.store(pack_view(ViewState::Waiting, generation_), std::memory_order_release);
    }
}

void M1State::request_confirmation(const std::uint32_t generation) {
    confirmation_generation_.store(generation, std::memory_order_release);
}

bool M1State::acquire_frame(Frame& frame) {
    if (slot_.load(std::memory_order_acquire) != Slot::Ready) {
        return false;
    }
    frame = handed_off_;
    slot_.store(Slot::Owned, std::memory_order_release);
    return true;
}

void M1State::release_frame() {
    slot_.store(Slot::Empty, std::memory_order_release);
}

}  // namespace gbp::terminal
