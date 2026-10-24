#include "ui/ui_shell.hpp"

#include <algorithm>
#include <array>
#include <atomic>

#include "board/board.hpp"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace apphub::ui {
namespace {

constexpr char kTag[] = "ui.shell";
constexpr int kWidth = 240;
constexpr int kHeight = 320;
constexpr int kHeaderHeight = 42;
constexpr int kFooterHeight = 38;
constexpr int kDrawBufferLines = 40;

lv_display_t* display{};
lv_indev_t* touch_input{};
lv_obj_t* content{};
lv_obj_t* title_label{};
lv_obj_t* page_label{};
std::atomic_bool settings_requested{false};
std::atomic_int navigation_requested{0};

void settings_clicked(lv_event_t*) {
    settings_requested.store(true, std::memory_order_release);
}

void previous_clicked(lv_event_t*) {
    navigation_requested.store(-1, std::memory_order_release);
}

void next_clicked(lv_event_t*) {
    navigation_requested.store(1, std::memory_order_release);
}

void style_panel(lv_obj_t* object, const lv_color_t color) {
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

lv_obj_t* make_nav_button(lv_obj_t* parent, const char* symbol,
                          lv_event_cb_t callback) {
    auto* button = lv_button_create(parent);
    lv_obj_set_size(button, 52, 30);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x243044), 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    return button;
}

}  // namespace

esp_err_t initialize() noexcept {
    if (display != nullptr) {
        return ESP_OK;
    }
    if (board::display_io_handle() == nullptr || board::display_handle() == nullptr ||
        board::touch_handle() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    port_config.task_priority = 4;
    port_config.task_stack = 8 * 1024;
    port_config.task_max_sleep_ms = 100;
    auto result = lvgl_port_init(&port_config);
    if (result != ESP_OK) {
        return result;
    }

    lvgl_port_display_cfg_t display_config{};
    display_config.io_handle = board::display_io_handle();
    display_config.panel_handle = board::display_handle();
    display_config.buffer_size = kWidth * kDrawBufferLines;
    display_config.double_buffer = true;
    display_config.hres = kWidth;
    display_config.vres = kHeight;
    display_config.monochrome = false;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    // ES3C28P cần MADCTL MX|BGR ở hướng dọc; thiếu mirror_x chữ sẽ bị ngược
    // như soi gương. lvgl_port tính tiếp swap/mirror cho các góc xoay khác.
    display_config.rotation.swap_xy = false;
    display_config.rotation.mirror_x = true;
    display_config.rotation.mirror_y = false;
    display_config.flags.buff_dma = true;
    display_config.flags.swap_bytes = true;
    display = lvgl_port_add_disp(&display_config);
    if (display == nullptr) {
        return ESP_FAIL;
    }

    lvgl_port_touch_cfg_t touch_config{};
    touch_config.disp = display;
    touch_config.handle = board::touch_handle();
    touch_input = lvgl_port_add_touch(&touch_config);
    if (touch_input == nullptr) {
        return ESP_FAIL;
    }

    if (!lock()) {
        return ESP_ERR_TIMEOUT;
    }

    // Header/content/footer xếp theo flex column nên tự co giãn khi đổi giữa
    // 240x320 (dọc) và 320x240 (ngang), app không cần biết hướng màn hình.
    auto* screen = lv_display_get_screen_active(display);
    style_panel(screen, lv_color_hex(0x0B1019));
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(screen, 0, 0);

    auto* header = lv_obj_create(screen);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    style_panel(header, lv_color_hex(0x121A27));
    lv_obj_set_scrollable(header, false);

    title_label = lv_label_create(header);
    lv_label_set_text(title_label, "App Hub");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xF4F7FB), 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 12, 0);

    auto* settings_button = lv_button_create(header);
    lv_obj_set_size(settings_button, 36, 32);
    lv_obj_align(settings_button, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_radius(settings_button, 8, 0);
    lv_obj_set_style_bg_color(settings_button, lv_color_hex(0x243044), 0);
    lv_obj_add_event_cb(settings_button, settings_clicked, LV_EVENT_CLICKED, nullptr);
    auto* settings_icon = lv_label_create(settings_button);
    lv_label_set_text(settings_icon, LV_SYMBOL_SETTINGS);
    lv_obj_center(settings_icon);

    content = lv_obj_create(screen);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    style_panel(content, lv_color_hex(0x0B1019));
    lv_obj_set_scrollable(content, false);

    auto* footer = lv_obj_create(screen);
    lv_obj_set_size(footer, LV_PCT(100), kFooterHeight);
    style_panel(footer, lv_color_hex(0x121A27));
    lv_obj_set_scrollable(footer, false);

    auto* previous = make_nav_button(footer, LV_SYMBOL_LEFT, previous_clicked);
    lv_obj_align(previous, LV_ALIGN_LEFT_MID, 6, 0);
    auto* next = make_nav_button(footer, LV_SYMBOL_RIGHT, next_clicked);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, -6, 0);

    page_label = lv_label_create(footer);
    lv_label_set_text(page_label, "auto");
    lv_obj_set_style_text_color(page_label, lv_color_hex(0x8EA0B8), 0);
    lv_obj_center(page_label);

    unlock();
    ESP_LOGI(kTag, "LVGL shell ready (%dx%d)", kWidth, kHeight);
    return ESP_OK;
}

void set_rotation(const std::uint16_t degrees) noexcept {
    if (display == nullptr) {
        return;
    }
    lv_display_rotation_t rotation = LV_DISPLAY_ROTATION_0;
    switch (degrees) {
        case 90:
            rotation = LV_DISPLAY_ROTATION_90;
            break;
        case 180:
            rotation = LV_DISPLAY_ROTATION_180;
            break;
        case 270:
            rotation = LV_DISPLAY_ROTATION_270;
            break;
        default:
            break;
    }
    if (lv_display_get_rotation(display) == rotation) {
        return;
    }
    // LVGL tự đảo tọa độ touch theo rotation; lvgl_port cập nhật MADCTL.
    lv_display_set_rotation(display, rotation);
    ESP_LOGI(kTag, "Rotation %u -> %dx%d", static_cast<unsigned>(degrees),
             static_cast<int>(lv_display_get_horizontal_resolution(display)),
             static_cast<int>(lv_display_get_vertical_resolution(display)));
}

bool is_ready() noexcept {
    return display != nullptr && content != nullptr;
}

bool lock(const std::uint32_t timeout_ms) noexcept {
    return lvgl_port_lock(timeout_ms);
}

void unlock() noexcept {
    lvgl_port_unlock();
}

lv_obj_t* content_root() noexcept {
    return content;
}

void set_title(const std::string_view title) noexcept {
    if (title_label == nullptr) {
        return;
    }
    const std::array<char, 48> text = [&title] {
        std::array<char, 48> buffer{};
        const auto length = std::min(title.size(), buffer.size() - 1);
        std::copy_n(title.begin(), length, buffer.begin());
        return buffer;
    }();
    lv_label_set_text(title_label, text.data());
}

void set_page_indicator(const std::string_view text) noexcept {
    if (page_label == nullptr) {
        return;
    }
    std::array<char, 24> buffer{};
    const auto length = std::min(text.size(), buffer.size() - 1);
    std::copy_n(text.begin(), length, buffer.begin());
    lv_label_set_text(page_label, buffer.data());
}

bool take_settings_request() noexcept {
    return settings_requested.exchange(false, std::memory_order_acq_rel);
}

int take_navigation_request() noexcept {
    return navigation_requested.exchange(0, std::memory_order_acq_rel);
}

}  // namespace apphub::ui
