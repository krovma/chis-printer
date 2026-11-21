// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/terminal/m1_state.h"

#include <cstdio>

#include "types.h"

namespace Gbp::Terminal {
namespace {

constexpr uint32 VIEW_MASK = 0x03;
constexpr uint32 MAX_GENERATION = 0x3FFFFFFF;

uint32 pack_view(const ViewState view, const uint32 generation) {
    return (generation << 2) | static_cast<uint32>(view);
}

}  // namespace

M1State::M1State(const uint32 auto_cut_time_limit_us)
    : receiver_(buffers_[0]), auto_cut_time_limit_us_(auto_cut_time_limit_us) {}

M1State::Snapshot M1State::snapshot(const uint32 now_us) const {
    const uint32 word = view_word_.load(std::memory_order_acquire);
    Snapshot result{static_cast<ViewState>(word & VIEW_MASK), frames_.load(std::memory_order_acquire),
                    word >> 2, cut_sequence_.load(std::memory_order_acquire)};
    result.cut_pending = cut_pending_.load(std::memory_order_acquire);
    if (result.cut_pending) {
        const auto elapsed = static_cast<uint32>(
            now_us - completed_at_published_us_.load(std::memory_order_acquire));
        result.cut_remaining_us = elapsed < auto_cut_time_limit_us_ ? auto_cut_time_limit_us_ - elapsed : 0;
    }
    result.discarded_frames = discarded_frames_.load(std::memory_order_acquire);
    return result;
}

void M1State::core1_on_print_complete(const uint32 at_us) {
    print_completed_at_us_ = at_us;
    completion_window_open_ = true;
    completed_at_published_us_.store(at_us, std::memory_order_release);
    cut_pending_.store(true, std::memory_order_release);
}

void M1State::core1_publish_cut() {
    completion_window_open_ = false;
    cut_pending_.store(false, std::memory_order_release);
    cut_sequence_.fetch_add(1, std::memory_order_acq_rel);
}

bool M1State::core1_expire_completion_window(const uint32 now_us) {
    if (completion_window_open_ && static_cast<uint32>(now_us - print_completed_at_us_) > auto_cut_time_limit_us_ &&
        static_cast<ViewState>(view_word_.load(std::memory_order_acquire) & VIEW_MASK) != ViewState::Receiving) {
        core1_publish_cut();
        return true;
    }
    return false;
}

void M1State::core1_pause_after_cut() {
    if (receiver_.state() == ViewState::Receiving) {
        receiver_.reset();
        frames_.store(0, std::memory_order_release);
        view_word_.store(pack_view(ViewState::Waiting, generation_), std::memory_order_release);
    }
}

void M1State::core1_on_receiver_change(const uint32 now_us) {
    if (receiver_.take_data_group_started()) {
        const bool within_window = completion_window_open_ && auto_cut_time_limit_us_ != 0 &&
            static_cast<uint32>(now_us - print_completed_at_us_) <= auto_cut_time_limit_us_;
        append_current_group_ = within_window;
        if (completion_window_open_) {
            if (within_window) {
                completion_window_open_ = false;
                cut_pending_.store(false, std::memory_order_release);
            } else {
                core1_publish_cut();
            }
        }
    }
    if (receiver_.state() == ViewState::Preview) {
        if (slot_.load(std::memory_order_acquire) != Slot::Empty) {
            printf("ERROR: M1 image discarded: previous frame still owned by Core0\n");
            discarded_frames_.fetch_add(1, std::memory_order_acq_rel);
            receiver_.reset();
            append_current_group_ = false;
            frames_.store(0, std::memory_order_release);
            view_word_.store(pack_view(ViewState::Waiting, generation_), std::memory_order_release);
            return;
        }

        const uint32 next_generation = generation_ == MAX_GENERATION ? 1 : generation_ + 1;
        handed_off_ = {receiver_.image_data(), receiver_.image_size(), receiver_.palette(),
                       receiver_.print_control(), next_generation, append_current_group_};
        append_current_group_ = false;
        // Core1 continues receiving in the other buffer while Core0 owns this frame.
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
    const uint32 requested_generation = confirmation_generation_.exchange(0, std::memory_order_acq_rel);
    const uint32 word = view_word_.load(std::memory_order_relaxed);
    if (requested_generation != 0 && requested_generation == (word >> 2) &&
        static_cast<ViewState>(word & VIEW_MASK) == ViewState::Preview) {
        view_word_.store(pack_view(ViewState::Waiting, generation_), std::memory_order_release);
    }
}

void M1State::request_confirmation(const uint32 generation) {
    confirmation_generation_.store(generation, std::memory_order_release);
}

bool M1State::acquire_frame(Frame& frame) {
    if (slot_.load(std::memory_order_acquire) != Slot::Ready) {
        return false;
    }
    frame = handed_off_;
    // Core0 must release the frame before Core1 can hand off another one.
    slot_.store(Slot::Owned, std::memory_order_release);
    return true;
}

void M1State::release_frame() {
    slot_.store(Slot::Empty, std::memory_order_release);
}

}  // namespace Gbp::Terminal
