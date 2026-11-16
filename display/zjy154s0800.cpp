#include "display/zjy154s0800.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "pin_config.h"

namespace display {
namespace {

// ST7789VW command codes, named as in the controller data sheet.
enum Command : uint8_t {
    SLEEP_OUT                  = 0x11,
    INVERSION_ON               = 0x21,
    DISPLAY_ON                 = 0x29,
    COLUMN_ADDRESS_SET         = 0x2A,
    ROW_ADDRESS_SET            = 0x2B,
    MEMORY_WRITE               = 0x2C,
    MEMORY_DATA_ACCESS_CONTROL = 0x36,
    INTERFACE_PIXEL_FORMAT     = 0x3A,
    PORCH_SETTING              = 0xB2,
    GATE_CONTROL               = 0xB7,
    VCOM_SETTING               = 0xBB,
    VDV_VRH_COMMAND_ENABLE     = 0xC2,
    VRH_SET                    = 0xC3,
    VDV_SET                    = 0xC4,
    FRAME_RATE_CONTROL_2       = 0xC6,
    POWER_CONTROL_1            = 0xD0,
    POSITIVE_GAMMA_CONTROL     = 0xE0,
    NEGATIVE_GAMMA_CONTROL     = 0xE1,
};

constexpr uint16_t BACKLIGHT_PWM_WRAP          = 999;
constexpr float    BACKLIGHT_PWM_CLOCK_DIVIDER = 125.0f;
constexpr size_t   PIXEL_COUNT                 = static_cast<size_t>(WIDTH) * HEIGHT;
constexpr size_t   PACKED_PIXELS_PER_ROW       = WIDTH / 2;
constexpr size_t   TRANSFER_ROW_SIZE           = static_cast<size_t>(WIDTH) * 3 / 2;
constexpr uint     FRAME_DMA_IRQ_INDEX          = 1;
static_assert(WIDTH % 2 == 0, "Packed 4bpp grayscale requires an even display width");

uint                                      frame_tx_dma_channel;
uint                                      frame_rx_dma_channel;
std::array<uint8_t, FRAME_BUFFER_SIZE>    frame_buffer{};
std::array<uint8_t, TRANSFER_ROW_SIZE>    transfer_row{};
uint8_t                                   frame_rx_sink;
volatile uint16_t                         next_transfer_row;
volatile bool                             frame_transfer_active;
volatile bool                             backlight_pending = true;
volatile uint8_t                          current_backlight_key = DEFAULT_BACKLIGHT_KEY;
volatile uint16_t                         requested_backlight_level =
    (BACKLIGHT_PWM_WRAP + 1) * BACKLIGHT_DUTY_PERCENT[DEFAULT_BACKLIGHT_KEY] / 100;
uint8_t                                   current_palette_key = WHITE_PALETTE_KEY;
volatile uint8_t                          transfer_palette_key = WHITE_PALETTE_KEY;

inline void select() {
    gpio_put(board::TFT_CS_N, false);
}

inline void deselect() {
    gpio_put(board::TFT_CS_N, true);
}

void write_command(const uint8_t command) {
    gpio_put(board::TFT_DC, false);
    select();
    spi_write_blocking(board::TFT_SPI, &command, 1);
    deselect();
}

void write_data(const uint8_t* data, const size_t length) {
    gpio_put(board::TFT_DC, true);
    select();
    spi_write_blocking(board::TFT_SPI, data, length);
    deselect();
}

template <size_t Size>
void write_register(const uint8_t command, const std::array<uint8_t, Size>& data) {
    write_command(command);
    write_data(data.data(), data.size());
}

void reset_panel() {
    // ZJY's vendor sequence specifies 100 ms low and 100 ms high. The ST7789
    // data sheet only requires a much shorter valid reset pulse and at least
    // 5 ms after release, so the module-specific delays are conservative.
    gpio_put(board::TFT_RESET_N, false);
    sleep_ms(100);
    gpio_put(board::TFT_RESET_N, true);
    sleep_ms(100);
}

void initialize_controller() {
    write_command(SLEEP_OUT);
    sleep_ms(120);

    // MADCTL: normal top-to-bottom, left-to-right memory order; RGB color
    // order. This is the portrait orientation used by the vendor sequence.
    write_register(MEMORY_DATA_ACCESS_CONTROL, std::array<uint8_t, 1>{0x00});

    // COLMOD: 0x03 selects 12-bit RGB444 data on the MCU interface. In the
    // four-wire serial format, two pixels are packed into three bytes as
    // [R1 G1] [B1 R2] [G2 B2].
    write_register(INTERFACE_PIXEL_FORMAT, std::array<uint8_t, 1>{0x03});

    // PORCTRL: front/back porch timing for normal and idle modes. Values below
    // are the ZJY module vendor's sequence, not controller reset defaults.
    write_register(PORCH_SETTING, std::array<uint8_t, 5>{0x0C, 0x0C, 0x00, 0x33, 0x33});

    // GCTRL: positive and negative gate-driver voltage selection.
    write_register(GATE_CONTROL, std::array<uint8_t, 1>{0x35});

    // VCOMS: common-electrode voltage. ZJY specifies 0x32 (about 1.35 V).
    write_register(VCOM_SETTING, std::array<uint8_t, 1>{0x32});

    // Allow VRH and VDV to be sourced from the following command writes
    // instead of the controller's nonvolatile defaults.
    write_register(VDV_VRH_COMMAND_ENABLE, std::array<uint8_t, 1>{0x01});

    // VRHS: GVDD/source-driver voltage level selected by the module vendor.
    write_register(VRH_SET, std::array<uint8_t, 1>{0x15});

    // VDVS: 0x20 corresponds to a 0 V VDV offset in the ST7789 table.
    write_register(VDV_SET, std::array<uint8_t, 1>{0x20});

    // FRCTRL2: 0x0F selects the vendor's normal-mode frame rate setting,
    // documented in the module sequence as 60 Hz.
    write_register(FRAME_RATE_CONTROL_2, std::array<uint8_t, 1>{0x0F});

    // PWCTRL1: AVDD, AVCL and VDDS power-rail settings for this panel.
    write_register(POWER_CONTROL_1, std::array<uint8_t, 2>{0xA4, 0xA1});

    // Module-specific positive gamma curve. Each byte controls one or more
    // points on the ST7789 positive-polarity gamma characteristic.
    write_register(POSITIVE_GAMMA_CONTROL,
                   std::array<uint8_t, 14>{0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31,
                                           0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34});

    // Module-specific negative gamma curve. ZJY supplies the same endpoint
    // values as the positive curve, with the third control point at 0x0E.
    write_register(NEGATIVE_GAMMA_CONTROL,
                   std::array<uint8_t, 14>{0xD0, 0x08, 0x0E, 0x09, 0x09, 0x15, 0x31,
                                           0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34});

    // This panel is specified for inversion-on operation.
    write_command(INVERSION_ON);

    // Enable panel scanning after all analog and gamma settings are loaded.
    write_command(DISPLAY_ON);
    sleep_ms(20);
}

void set_address_window(const uint16_t x0, const uint16_t y0, const uint16_t x1, const uint16_t y1) {
    const std::array<uint8_t, 4> columns{
        static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0),
        static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1)};
    write_register(COLUMN_ADDRESS_SET, columns);

    const std::array<uint8_t, 4> rows{
        static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0),
        static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1)};
    write_register(ROW_ADDRESS_SET, rows);

    // RAMWR makes all subsequent data bytes pixel data until another command.
    write_command(MEMORY_WRITE);
}

void initialize_backlight_pwm() {
    // BLK is active-high. A 125 MHz default system clock divided by 125 and a
    // wrap value of 999 produces a 1 kHz PWM carrier.
    gpio_set_function(board::TFT_BACKLIGHT, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(board::TFT_BACKLIGHT);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, BACKLIGHT_PWM_CLOCK_DIVIDER);
    pwm_config_set_wrap(&config, BACKLIGHT_PWM_WRAP);
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(board::TFT_BACKLIGHT, 0);
}

void apply_backlight_level() {
    pwm_set_gpio_level(board::TFT_BACKLIGHT, requested_backlight_level);
    backlight_pending = false;
}

void prepare_transfer_row(const uint16_t row) {
    const size_t source_offset = static_cast<size_t>(row) * PACKED_PIXELS_PER_ROW;
    const uint16_t base       = BASE_PALETTE_RGB444[transfer_palette_key];

    // Each source byte stores two grayscale pixels, G1:G2. Tint both pixels
    // through the selected base color before packing RGB444 serial bytes.
    for (size_t source_index = 0, target_index = 0; source_index < PACKED_PIXELS_PER_ROW; ++source_index, target_index += 3) {
        detail::pack_gray_pair(frame_buffer[source_offset + source_index], base, transfer_row.data() + target_index);
    }
}

void start_transfer_row_dma() {
    const uint32_t transfer_count = dma_encode_transfer_count(TRANSFER_ROW_SIZE);

    // Arm RX first so it drains the full-duplex SPI receive FIFO before TX
    // starts producing clock edges for the expanded RGB444 row.
    dma_channel_transfer_to_buffer_now(frame_rx_dma_channel, &frame_rx_sink, transfer_count);
    dma_channel_transfer_from_buffer_now(frame_tx_dma_channel, transfer_row.data(), transfer_count);
}

void frame_dma_irq_handler() {
    if (!dma_irqn_get_channel_status(FRAME_DMA_IRQ_INDEX, frame_rx_dma_channel)) {
        return;
    }

    dma_irqn_acknowledge_channel(FRAME_DMA_IRQ_INDEX, frame_rx_dma_channel);

    if (next_transfer_row < HEIGHT) {
        prepare_transfer_row(next_transfer_row++);
        start_transfer_row_dma();
        return;
    }

    deselect();
    frame_transfer_active = false;
    if (backlight_pending) {
        apply_backlight_level();
    }
}

void initialize_frame_dma() {
    frame_tx_dma_channel = dma_claim_unused_channel(true);
    dma_channel_config tx_config = dma_channel_get_default_config(frame_tx_dma_channel);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    channel_config_set_dreq(&tx_config, spi_get_dreq(board::TFT_SPI, true));
    dma_channel_configure(frame_tx_dma_channel, &tx_config, &spi_get_hw(board::TFT_SPI)->dr, transfer_row.data(), 0, false);

    // SPI receives one byte while transmitting each byte. Drain those bytes
    // with a second DMA channel so the RX FIFO cannot overrun during a frame.
    frame_rx_dma_channel = dma_claim_unused_channel(true);
    dma_channel_config rx_config = dma_channel_get_default_config(frame_rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, false);
    channel_config_set_dreq(&rx_config, spi_get_dreq(board::TFT_SPI, false));
    dma_channel_configure(frame_rx_dma_channel, &rx_config, &frame_rx_sink, &spi_get_hw(board::TFT_SPI)->dr, 0, false);

    dma_irqn_acknowledge_channel(FRAME_DMA_IRQ_INDEX, frame_rx_dma_channel);
    dma_irqn_set_channel_enabled(FRAME_DMA_IRQ_INDEX, frame_rx_dma_channel, true);
    irq_add_shared_handler(DMA_IRQ_NUM(FRAME_DMA_IRQ_INDEX), frame_dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_NUM(FRAME_DMA_IRQ_INDEX), true);
}

}  // namespace

void init() {
    // The ZJY timing table leaves numeric limits as TBD. Start conservatively
    // at 20 MHz. The reviewed STM32 hardware-SPI example uses mode 3 and MSB
    // first; ST7789 samples SDA on the rising edge, which mode 3 provides.
    spi_init(board::TFT_SPI, board::TFT_SPI_BAUD);
    spi_set_format(board::TFT_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(board::TFT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(board::TFT_MOSI, GPIO_FUNC_SPI);

    gpio_init(board::TFT_CS_N);
    gpio_put(board::TFT_CS_N, true);
    gpio_set_dir(board::TFT_CS_N, GPIO_OUT);

    gpio_init(board::TFT_DC);
    gpio_put(board::TFT_DC, false);
    gpio_set_dir(board::TFT_DC, GPIO_OUT);

    gpio_init(board::TFT_RESET_N);
    gpio_put(board::TFT_RESET_N, true);
    gpio_set_dir(board::TFT_RESET_N, GPIO_OUT);

    initialize_frame_dma();
    initialize_backlight_pwm();
    backlight_pending = true;
    reset_panel();
    initialize_controller();
    clear(0x000);
}

void set_backlight(const float percent) {
    const float clamped = std::clamp(percent, 0.0f, 100.0f);
    requested_backlight_level = static_cast<uint16_t>((clamped * static_cast<float>(BACKLIGHT_PWM_WRAP + 1)) / 100.0f);
    if (!backlight_pending) {
        apply_backlight_level();
    }
}

bool set_backlight_key(const uint8_t key) {
    if (key >= BACKLIGHT_DUTY_PERCENT.size()) {
        return false;
    }

    current_backlight_key = key;
    requested_backlight_level = static_cast<uint16_t>((BACKLIGHT_PWM_WRAP + 1) * BACKLIGHT_DUTY_PERCENT[key] / 100);
    if (!backlight_pending) {
        apply_backlight_level();
    }
    return true;
}

uint8_t backlight_key() {
    return current_backlight_key;
}

bool set_palette_key(const uint8_t key) {
    if (key >= PALETTE_COUNT) {
        return false;
    }

    current_palette_key = key;
    return true;
}

uint8_t palette_key() {
    return current_palette_key;
}

void clear(const Gray4 gray) {
    const uint8_t packed_gray = static_cast<uint8_t>((gray & 0x0F) * 0x11);
    frame_buffer.fill(packed_gray);
}

void set_pixel(const uint16_t x, const uint16_t y, const Gray4 gray) {
    if (x >= WIDTH || y >= HEIGHT) {
        return;
    }

    const uint8_t packed_gray = gray & 0x0F;
    const size_t  pixel_index = static_cast<size_t>(y) * WIDTH + x;
    const size_t  byte_offset = pixel_index / 2;

    if ((pixel_index & 1u) == 0) {
        frame_buffer[byte_offset] = static_cast<uint8_t>((frame_buffer[byte_offset] & 0x0F) | (packed_gray << 4));
    } else {
        frame_buffer[byte_offset] = static_cast<uint8_t>((frame_buffer[byte_offset] & 0xF0) | packed_gray);
    }
}

void fill_circle(const int16_t center_x, const int16_t center_y, const int16_t radius, const Gray4 gray) {
    if (radius <= 0) {
        return;
    }

    const int32_t radius_squared = static_cast<int32_t>(radius) * static_cast<int32_t>(radius);
    for (int32_t y_offset = -radius; y_offset <= radius; ++y_offset) {
        const int32_t y = static_cast<int32_t>(center_y) + y_offset;
        if (y < 0 || y >= HEIGHT) {
            continue;
        }

        int32_t x_extent = radius;
        while (x_extent * x_extent + y_offset * y_offset > radius_squared) {
            --x_extent;
        }

        const int32_t first_x = std::max<int32_t>(0, static_cast<int32_t>(center_x) - x_extent);
        const int32_t last_x  = std::min<int32_t>(WIDTH - 1, static_cast<int32_t>(center_x) + x_extent);
        for (int32_t x = first_x; x <= last_x; ++x) {
            set_pixel(static_cast<uint16_t>(x), static_cast<uint16_t>(y), gray);
        }
    }
}

bool present() {
    if (frame_transfer_active) {
        return false;
    }

    transfer_palette_key = current_palette_key;
    frame_transfer_active = true;
    set_address_window(0, 0, WIDTH - 1, HEIGHT - 1);

    gpio_put(board::TFT_DC, true);
    select();

    prepare_transfer_row(0);
    next_transfer_row = 1;
    start_transfer_row_dma();
    return true;
}

bool is_presenting() {
    return frame_transfer_active;
}

}  // namespace display
