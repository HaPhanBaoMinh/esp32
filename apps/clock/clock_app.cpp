#include "apps/clock_app.hpp"

#include <ctime>
#include <cstdio>

#include "esp_log.h"
#include "ui/ui_shell.hpp"

namespace apphub::apps {
namespace {

constexpr char kTag[] = "app.clock";
constexpr AppMetadata kMetadata{
    .id = "clock",
    .title = "Clock",
    .rotatable = true,
    .enabled_by_default = true,
    .default_duration_seconds = 60,
};

}  // namespace

const AppMetadata& ClockApp::metadata() const noexcept {
    return kMetadata;
}

void ClockApp::on_enter() {
    ESP_LOGI(kTag, "enter");
    last_log_ms_ = 0;
    ui::set_title("Clock");
    ui::set_page_indicator("auto");
    if (ui::content_root() == nullptr) {
        return;
    }

    root_ = lv_obj_create(ui::content_root());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x0B1019), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_scrollable(root_, false);

    time_label_ = lv_label_create(root_);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(time_label_, lv_color_hex(0xF4F7FB), 0);
    lv_obj_align(time_label_, LV_ALIGN_CENTER, 0, -18);

    date_label_ = lv_label_create(root_);
    lv_label_set_text(date_label_, "Waiting for time sync");
    lv_obj_set_style_text_color(date_label_, lv_color_hex(0x8EA0B8), 0);
    lv_obj_align(date_label_, LV_ALIGN_CENTER, 0, 34);
}

void ClockApp::on_exit() {
    ESP_LOGI(kTag, "exit");
    if (root_ != nullptr) {
        lv_obj_delete(root_);
    }
    root_ = nullptr;
    time_label_ = nullptr;
    date_label_ = nullptr;
}

void ClockApp::on_tick(const std::uint64_t now_ms) {
    if (last_log_ms_ != 0 && now_ms - last_log_ms_ < 1'000) {
        return;
    }
    last_log_ms_ = now_ms;

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    ESP_LOGI(kTag, "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);

    if (time_label_ == nullptr || date_label_ == nullptr) {
        return;
    }
    char time_text[8]{};
    std::snprintf(time_text, sizeof(time_text), "%02d:%02d", local.tm_hour, local.tm_min);
    lv_label_set_text(time_label_, time_text);

    char date_text[32]{};
    if (local.tm_year + 1900 >= 2024) {
        std::strftime(date_text, sizeof(date_text), "%a, %d/%m/%Y", &local);
    } else {
        std::snprintf(date_text, sizeof(date_text), "Waiting for time sync");
    }
    lv_label_set_text(date_label_, date_text);
}

}  // namespace apphub::apps

