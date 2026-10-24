#include "board/board.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace apphub::board {
namespace {

constexpr char kTag[] = "board.es3c28p";
constexpr int kWidth = 240;
constexpr int kHeight = 320;
constexpr int kStripeHeight = 40;

constexpr gpio_num_t kLcdCs = GPIO_NUM_10;
constexpr gpio_num_t kLcdDc = GPIO_NUM_46;
constexpr gpio_num_t kLcdSclk = GPIO_NUM_12;
constexpr gpio_num_t kLcdMosi = GPIO_NUM_11;
constexpr gpio_num_t kLcdMiso = GPIO_NUM_13;
constexpr gpio_num_t kBacklight = GPIO_NUM_45;

constexpr gpio_num_t kTouchSda = GPIO_NUM_16;
constexpr gpio_num_t kTouchScl = GPIO_NUM_15;
constexpr gpio_num_t kTouchReset = GPIO_NUM_18;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_17;

constexpr spi_host_device_t kLcdSpiHost = SPI2_HOST;
constexpr ledc_mode_t kBacklightSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
constexpr std::uint32_t kBacklightMaxDuty = (1U << 10U) - 1U;

esp_lcd_panel_io_handle_t lcd_io{};
esp_lcd_panel_handle_t lcd_panel{};
i2c_master_bus_handle_t touch_i2c_bus{};
esp_lcd_panel_io_handle_t touch_io{};
esp_lcd_touch_handle_t touch_panel{};
SemaphoreHandle_t color_transfer_done{};
bool backlight_ready{false};
bool display_ready{false};
bool touch_ready{false};

bool on_color_transfer_done(esp_lcd_panel_io_handle_t,
                            esp_lcd_panel_io_event_data_t*, void* user_context) {
    auto* semaphore = static_cast<SemaphoreHandle_t*>(user_context);
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(*semaphore, &task_woken);
    return task_woken == pdTRUE;
}

std::uint16_t spi_rgb565(const std::uint16_t value) noexcept {
    return static_cast<std::uint16_t>((value << 8U) | (value >> 8U));
}

esp_err_t initialize_backlight() noexcept {
    ledc_timer_config_t timer{};
    timer.speed_mode = kBacklightSpeedMode;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = kBacklightTimer;
    timer.freq_hz = 10'000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    auto result = ledc_timer_config(&timer);
    if (result != ESP_OK) {
        return result;
    }

    ledc_channel_config_t channel{};
    channel.gpio_num = kBacklight;
    channel.speed_mode = kBacklightSpeedMode;
    channel.channel = kBacklightChannel;
    channel.timer_sel = kBacklightTimer;
    channel.duty = 0;
    channel.hpoint = 0;
    result = ledc_channel_config(&channel);
    if (result == ESP_OK) {
        backlight_ready = true;
    }
    return result;
}

esp_err_t initialize_display() noexcept {
    color_transfer_done = xSemaphoreCreateBinary();
    if (color_transfer_done == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus{};
    bus.mosi_io_num = kLcdMosi;
    bus.miso_io_num = kLcdMiso;
    bus.sclk_io_num = kLcdSclk;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = kWidth * kStripeHeight * sizeof(std::uint16_t);
    auto result = spi_bus_initialize(kLcdSpiHost, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) {
        return result;
    }

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = kLcdCs;
    io_config.dc_gpio_num = kLcdDc;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 1;
    io_config.on_color_trans_done = on_color_transfer_done;
    io_config.user_ctx = &color_transfer_done;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    result = esp_lcd_new_panel_io_spi(kLcdSpiHost, &io_config, &lcd_io);
    if (result != ESP_OK) {
        return result;
    }

    esp_lcd_panel_dev_config_t panel_config{};
    // LCD reset is tied to ESP32-S3 CHIP_PU, so use a software reset.
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;
    result = esp_lcd_new_panel_ili9341(lcd_io, &panel_config, &lcd_panel);
    if (result != ESP_OK) {
        return result;
    }

    if ((result = esp_lcd_panel_reset(lcd_panel)) != ESP_OK ||
        (result = esp_lcd_panel_init(lcd_panel)) != ESP_OK ||
        (result = esp_lcd_panel_invert_color(lcd_panel, true)) != ESP_OK ||
        (result = esp_lcd_panel_disp_on_off(lcd_panel, true)) != ESP_OK) {
        return result;
    }

    display_ready = true;
    return ESP_OK;
}

esp_err_t initialize_touch() noexcept {
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = kTouchSda;
    bus_config.scl_io_num = kTouchScl;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    auto result = i2c_new_master_bus(&bus_config, &touch_i2c_bus);
    if (result != ESP_OK) {
        return result;
    }

    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    io_config.scl_speed_hz = 100'000;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.flags.disable_control_phase = 1;
    result = esp_lcd_new_panel_io_i2c(touch_i2c_bus, &io_config, &touch_io);
    if (result != ESP_OK) {
        return result;
    }

    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = kWidth;
    touch_config.y_max = kHeight;
    touch_config.rst_gpio_num = kTouchReset;
    touch_config.int_gpio_num = kTouchInterrupt;
    touch_config.levels.reset = 0;
    touch_config.levels.interrupt = 0;
    result = esp_lcd_touch_new_i2c_ft5x06(touch_io, &touch_config, &touch_panel);
    if (result == ESP_OK) {
        touch_ready = true;
    }
    return result;
}

}  // namespace

Info probe() noexcept {
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    std::uint32_t flash_size = 0;
    if (esp_flash_get_size(nullptr, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    return {
        .variant = "LCDWiki-ES3C28P",
        .chip_revision = chip.revision,
        .flash_bytes = flash_size,
        .psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
        .display_ready = display_ready,
        .touch_ready = touch_ready,
    };
}

esp_err_t initialize_display_and_touch(const std::uint8_t brightness_percent) noexcept {
    ESP_LOGI(kTag, "Initializing LCDWiki ES3C28P (ILI9341V + FT6336G)");

    auto result = initialize_backlight();
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Backlight init failed: %s", esp_err_to_name(result));
        return result;
    }
    if ((result = initialize_display()) != ESP_OK) {
        ESP_LOGE(kTag, "Display init failed: %s", esp_err_to_name(result));
        return result;
    }
    if ((result = draw_bringup_pattern()) != ESP_OK) {
        ESP_LOGE(kTag, "Display test pattern failed: %s", esp_err_to_name(result));
        return result;
    }
    if ((result = set_display_brightness(brightness_percent)) != ESP_OK) {
        ESP_LOGE(kTag, "Backlight enable failed: %s", esp_err_to_name(result));
        return result;
    }
    if ((result = initialize_touch()) != ESP_OK) {
        ESP_LOGE(kTag, "Touch init failed: %s", esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(kTag, "Display and touch self-test initialized");
    return ESP_OK;
}

esp_err_t set_display_brightness(const std::uint8_t brightness_percent) noexcept {
    if (!backlight_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const auto percent = std::min<std::uint32_t>(brightness_percent, 100U);
    const auto duty = kBacklightMaxDuty * percent / 100U;
    auto result = ledc_set_duty(kBacklightSpeedMode, kBacklightChannel, duty);
    if (result == ESP_OK) {
        result = ledc_update_duty(kBacklightSpeedMode, kBacklightChannel);
    }
    return result;
}

esp_err_t draw_bringup_pattern() noexcept {
    if (!display_ready || color_transfer_done == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    constexpr std::array<std::uint16_t, 8> colors{
        0xF800,  // red
        0x07E0,  // green
        0x001F,  // blue
        0xFFE0,  // yellow
        0x07FF,  // cyan
        0xF81F,  // magenta
        0xFFFF,  // white
        0x0000,  // black
    };

    const auto pixel_count = static_cast<std::size_t>(kWidth * kStripeHeight);
    auto* stripe = static_cast<std::uint16_t*>(
        heap_caps_malloc(pixel_count * sizeof(std::uint16_t),
                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (stripe == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = ESP_OK;
    for (std::size_t index = 0; index < colors.size(); ++index) {
        std::fill_n(stripe, pixel_count, spi_rgb565(colors[index]));
        const auto y_start = static_cast<int>(index) * kStripeHeight;
        result = esp_lcd_panel_draw_bitmap(lcd_panel, 0, y_start, kWidth,
                                           y_start + kStripeHeight, stripe);
        if (result != ESP_OK ||
            xSemaphoreTake(color_transfer_done, pdMS_TO_TICKS(1'000)) != pdTRUE) {
            if (result == ESP_OK) {
                result = ESP_ERR_TIMEOUT;
            }
            break;
        }
    }

    heap_caps_free(stripe);
    return result;
}

bool read_touch(TouchPoint& point) noexcept {
    if (!touch_ready || esp_lcd_touch_read_data(touch_panel) != ESP_OK) {
        return false;
    }

    std::uint8_t count = 0;
    esp_lcd_touch_point_data_t data{};
    if (esp_lcd_touch_get_data(touch_panel, &data, &count, 1) != ESP_OK || count == 0) {
        return false;
    }

    point = {.x = data.x, .y = data.y, .strength = data.strength};
    return true;
}

esp_lcd_panel_handle_t display_handle() noexcept {
    return lcd_panel;
}

esp_lcd_panel_io_handle_t display_io_handle() noexcept {
    return lcd_io;
}

esp_lcd_touch_handle_t touch_handle() noexcept {
    return touch_panel;
}

}  // namespace apphub::board
