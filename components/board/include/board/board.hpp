#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

namespace apphub::board {

struct Info {
    const char* variant;
    std::uint32_t chip_revision;
    std::size_t flash_bytes;
    std::size_t psram_bytes;
    bool display_ready;
    bool touch_ready;
};

Info probe() noexcept;

struct TouchPoint {
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t strength;
};

// Board support for LCDWiki ES3C28P: ILI9341V + FT6336G.
esp_err_t initialize_display_and_touch(std::uint8_t brightness_percent) noexcept;
esp_err_t set_display_brightness(std::uint8_t brightness_percent) noexcept;
esp_err_t draw_bringup_pattern() noexcept;
bool read_touch(TouchPoint& point) noexcept;

esp_lcd_panel_handle_t display_handle() noexcept;
esp_lcd_panel_io_handle_t display_io_handle() noexcept;
esp_lcd_touch_handle_t touch_handle() noexcept;

}  // namespace apphub::board

