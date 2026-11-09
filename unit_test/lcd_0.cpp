#include "unit_test/lcd_0.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>

#include "pico/rand.h"
#include "pico/time.h"

#include "display/zjy154s0800.h"

namespace unit_test::lcd_0 {
namespace {

constexpr std::array<uint8_t, 6> BACKLIGHT_LEVELS{10, 25, 50, 75, 88, 100};

constexpr uint32_t BACKLIGHT_STEP_MS     = 500;
constexpr uint32_t FRAME_STEP_MS         = 50;
constexpr uint16_t MIN_RADIUS            = 8;
constexpr uint16_t MAX_RADIUS            = 72;
constexpr uint16_t GRAYSCALE_COUNT       = 16;
constexpr uint16_t GRAYSCALE_BAR_WIDTH   = display::WIDTH / GRAYSCALE_COUNT;
constexpr uint16_t GRAYSCALE_SCROLL_STEP = 1;
static_assert(display::WIDTH % GRAYSCALE_COUNT == 0, "Display width must divide evenly into sixteen grayscale bars");

struct State {
    absolute_time_t next_backlight_update;
    absolute_time_t next_frame_update;
    size_t          backlight_index;
    uint16_t        grayscale_offset;
    bool            grayscale_frame_ready;
};

State state{};

uint32_t random_inclusive(const uint32_t minimum, const uint32_t maximum) {
    return minimum + get_rand_32() % (maximum - minimum + 1);
}

[[maybe_unused]] bool draw_random_frame() {
    const uint16_t       radius   = static_cast<uint16_t>(random_inclusive(MIN_RADIUS, MAX_RADIUS));
    const uint16_t       center_x = static_cast<uint16_t>(random_inclusive(radius, display::WIDTH - 1 - radius));
    const uint16_t       center_y = static_cast<uint16_t>(random_inclusive(radius, display::HEIGHT - 1 - radius));
    // Exclude black so the circle is always visible on the cleared frame.
    const display::Gray4 gray     = static_cast<display::Gray4>(random_inclusive(1, 0x0F));

    display::clear(0x0);
    display::fill_circle(static_cast<int16_t>(center_x), static_cast<int16_t>(center_y), static_cast<int16_t>(radius), gray);
    if (!display::present()) {
        return false;
    }

    const uint16_t diameter = static_cast<uint16_t>(radius * 2 + 1);
    const uint16_t rgb444   = static_cast<uint16_t>(gray) * 0x111;
    printf("LCD circle: size=%ux%u, center=(%u,%u), radius=%u, color=#%03X\n",
           static_cast<unsigned>(diameter),
           static_cast<unsigned>(diameter),
           static_cast<unsigned>(center_x),
           static_cast<unsigned>(center_y),
           static_cast<unsigned>(radius),
           static_cast<unsigned>(rgb444));
    return true;
}

void draw_grayscale_bars() {
    for (uint16_t y = 0; y < display::HEIGHT; ++y) {
        for (uint16_t x = 0; x < display::WIDTH; ++x) {
            const uint16_t shifted_x = static_cast<uint16_t>((x + state.grayscale_offset) % display::WIDTH);
            const auto     gray      = static_cast<display::Gray4>(shifted_x / GRAYSCALE_BAR_WIDTH);
            display::set_pixel(x, y, gray);
        }
    }
}

}  // namespace

void init() {
    display::init();

    state.backlight_index       = 0;
    state.grayscale_offset      = 0;
    state.grayscale_frame_ready = false;
    display::set_backlight(BACKLIGHT_LEVELS[state.backlight_index]);
    draw_grayscale_bars();
    display::present();

    const absolute_time_t now = get_absolute_time();
    state.next_backlight_update = delayed_by_ms(now, BACKLIGHT_STEP_MS);
    state.next_frame_update     = delayed_by_ms(now, FRAME_STEP_MS);
}

void update() {
    const absolute_time_t now = get_absolute_time();

    if (time_reached(state.next_backlight_update)) {
        state.backlight_index = (state.backlight_index + 1) % BACKLIGHT_LEVELS.size();
        display::set_backlight(BACKLIGHT_LEVELS[state.backlight_index]);
        state.next_backlight_update = delayed_by_ms(state.next_backlight_update, BACKLIGHT_STEP_MS);
    }

    if (time_reached(state.next_frame_update)) {
        if (!state.grayscale_frame_ready) {
            draw_grayscale_bars();
            state.grayscale_frame_ready = true;
        }

        if (display::present()) {
            state.grayscale_frame_ready = false;
            state.grayscale_offset      = static_cast<uint16_t>((state.grayscale_offset + GRAYSCALE_SCROLL_STEP) % display::WIDTH);
            state.next_frame_update     = delayed_by_ms(now, FRAME_STEP_MS);
        }
    }
}

}  // namespace unit_test::lcd_0
