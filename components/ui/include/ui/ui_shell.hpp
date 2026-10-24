#pragma once

#include <cstdint>
#include <string_view>

#include "esp_err.h"
#include "lvgl.h"

namespace apphub::ui {

esp_err_t initialize() noexcept;
bool is_ready() noexcept;

bool lock(std::uint32_t timeout_ms = 0) noexcept;
void unlock() noexcept;

// Gọi khi đang giữ lock(). degrees: 0, 90, 180 hoặc 270.
void set_rotation(std::uint16_t degrees) noexcept;

lv_obj_t* content_root() noexcept;
void set_title(std::string_view title) noexcept;
void set_page_indicator(std::string_view text) noexcept;

bool take_settings_request() noexcept;
int take_navigation_request() noexcept;

}  // namespace apphub::ui
