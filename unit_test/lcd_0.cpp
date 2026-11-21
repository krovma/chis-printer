// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "unit_test/lcd_0.h"

#include <cstdio>

#include "types.h"
#include "pico/rand.h"
#include "pico/time.h"

#include "display/zjy154s0800.h"

namespace UnitTest::Lcd0 {
namespace {

constexpr uint32 PALETTE_STEP_MS       = 1200;
constexpr uint32 FRAME_STEP_MS         = 50;
constexpr uint16 MIN_RADIUS            = 8;
constexpr uint16 MAX_RADIUS            = 72;
constexpr uint16 GRAYSCALE_COUNT       = 16;
constexpr uint16 GRAYSCALE_BAR_WIDTH   = Display::WIDTH / GRAYSCALE_COUNT;
constexpr uint16 GRAYSCALE_SCROLL_STEP = 1;
static_assert(Display::WIDTH % GRAYSCALE_COUNT == 0, "Display width must divide evenly into sixteen grayscale bars");

struct State {
    absolute_time_t next_palette_update;
    absolute_time_t next_frame_update;
    uint8 palette_key;
    uint16 grayscale_offset;
    bool grayscale_frame_ready;
};

State state{};

uint32 random_inclusive(const uint32 minimum, const uint32 maximum) {
    return minimum + get_rand_32() % (maximum - minimum + 1);
}

[[maybe_unused]] bool draw_random_frame() {
    const uint16 radius = static_cast<uint16>(random_inclusive(MIN_RADIUS, MAX_RADIUS));
    const uint16 center_x = static_cast<uint16>(random_inclusive(radius, Display::WIDTH - 1 - radius));
    const uint16 center_y = static_cast<uint16>(random_inclusive(radius, Display::HEIGHT - 1 - radius));
    // Exclude black so the circle is always visible on the cleared frame.
    const Display::Gray4 gray = static_cast<Display::Gray4>(random_inclusive(1, 0x0F));

    Display::clear(0x0);
    Display::fill_circle(static_cast<int16>(center_x), static_cast<int16>(center_y), static_cast<int16>(radius), gray);
    if (!Display::present()) {
        return false;
    }

    const uint16 diameter = static_cast<uint16>(radius * 2 + 1);
    const uint8 palette_key = Display::palette_key();
    const uint16 rgb444 = Display::Detail::tint_rgb444(gray, Display::BASE_PALETTE_RGB444[palette_key]);
    printf("LCD circle: size=%ux%u, center=(%u,%u), radius=%u, palette=%u, color=#%03X\n",
           static_cast<unsigned>(diameter),
           static_cast<unsigned>(diameter),
           static_cast<unsigned>(center_x),
           static_cast<unsigned>(center_y),
           static_cast<unsigned>(radius),
           static_cast<unsigned>(palette_key),
           static_cast<unsigned>(rgb444));
    return true;
}

void draw_grayscale_bars() {
    for (uint16 y = 0; y < Display::HEIGHT; ++y) {
        for (uint16 x = 0; x < Display::WIDTH; ++x) {
            const uint16 shifted_x = static_cast<uint16>((x + state.grayscale_offset) % Display::WIDTH);
            const auto     gray      = static_cast<Display::Gray4>(shifted_x / GRAYSCALE_BAR_WIDTH);
            Display::set_pixel(x, y, gray);
        }
    }
}

}  // namespace

void init() {
    Display::init();

    state.palette_key           = Display::WHITE_PALETTE_KEY;
    state.grayscale_offset      = 0;
    state.grayscale_frame_ready = false;
    Display::set_palette_key(state.palette_key);
    draw_grayscale_bars();
    Display::present();

    const absolute_time_t now = get_absolute_time();
    state.next_palette_update   = delayed_by_ms(now, PALETTE_STEP_MS);
    state.next_frame_update     = delayed_by_ms(now, FRAME_STEP_MS);
}

void update() {
    const absolute_time_t now = get_absolute_time();

    if (time_reached(state.next_palette_update)) {
        state.palette_key = static_cast<uint8>((state.palette_key + 1) % Display::PALETTE_COUNT);
        Display::set_palette_key(state.palette_key);
        printf("LCD palette: key=%u\n", static_cast<unsigned>(state.palette_key));
        state.next_palette_update = delayed_by_ms(state.next_palette_update, PALETTE_STEP_MS);
    }

    if (time_reached(state.next_frame_update)) {
        if (!state.grayscale_frame_ready) {
            draw_grayscale_bars();
            state.grayscale_frame_ready = true;
        }

        if (Display::present()) {
            state.grayscale_frame_ready = false;
            state.grayscale_offset      = static_cast<uint16>((state.grayscale_offset + GRAYSCALE_SCROLL_STEP) % Display::WIDTH);
            state.next_frame_update     = delayed_by_ms(now, FRAME_STEP_MS);
        }
    }
}

}  // namespace UnitTest::Lcd0
