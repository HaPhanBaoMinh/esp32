#include "apps/settings_app.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "board/board.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui/ui_shell.hpp"

namespace apphub::apps {
namespace {

constexpr char kTag[] = "app.settings";
constexpr AppMetadata kMetadata{
    .id = "settings",
    .title = "Settings",
    .rotatable = false,
    .enabled_by_default = false,
    .default_duration_seconds = 60,
};

lv_obj_t* create_row(lv_obj_t* parent, const char* title) {
    auto* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x121A27), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 7, 0);

    auto* label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F7FB), 0);
    return row;
}

bool event_checked(lv_event_t* event) {
    return lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)),
                            LV_STATE_CHECKED);
}

// Thứ tự option trong dropdown hướng màn hình, đơn vị độ.
constexpr std::uint16_t kRotations[] = {0, 90, 180, 270};

SettingsApp* event_app(lv_event_t* event) {
    return static_cast<SettingsApp*>(lv_event_get_user_data(event));
}

}  // namespace

const AppMetadata& SettingsApp::metadata() const noexcept {
    return kMetadata;
}

void SettingsApp::on_enter() {
    ui::set_title("Settings");
    ui::set_page_indicator("tap gear to close");
    if (ui::content_root() == nullptr) {
        return;
    }

    requested_auto_rotate_.store(settings_.auto_rotate);
    requested_brightness_.store(settings_.brightness_percent);
    const auto* clock_preference = find_app_preference(settings_, "clock");
    requested_duration_.store(clock_preference == nullptr
                                  ? kMetadata.default_duration_seconds
                                  : clock_preference->duration_seconds);
    requested_night_clock_.store(settings_.schedule_rule_count > 0 &&
                                 settings_.schedule_rules[0].enabled);

    root_ = lv_obj_create(ui::content_root());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x0B1019), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 8, 0);
    lv_obj_set_style_pad_row(root_, 8, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);

    auto* orientation_row = create_row(root_, "Screen orientation");
    auto* orientation = lv_dropdown_create(orientation_row);
    lv_obj_set_width(orientation, LV_PCT(100));
    lv_dropdown_set_options_static(orientation,
                                   "Portrait\nLandscape\nPortrait (flipped)\nLandscape (flipped)");
    for (std::uint32_t index = 0; index < std::size(kRotations); ++index) {
        if (kRotations[index] == settings_.rotation_degrees) {
            lv_dropdown_set_selected(orientation, index);
        }
    }
    lv_obj_add_event_cb(orientation, rotation_event, LV_EVENT_VALUE_CHANGED, this);

    auto* rotate_row = create_row(root_, "Auto rotate apps");
    auto* rotate_switch = lv_switch_create(rotate_row);
    if (settings_.auto_rotate) {
        lv_obj_add_state(rotate_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(rotate_switch, auto_rotate_event, LV_EVENT_VALUE_CHANGED, this);

    auto* brightness_row = create_row(root_, "Brightness");
    brightness_label_ = lv_label_create(brightness_row);
    lv_obj_set_style_text_color(brightness_label_, lv_color_hex(0x8EA0B8), 0);
    auto* brightness_slider = lv_slider_create(brightness_row);
    lv_obj_set_width(brightness_slider, LV_PCT(100));
    lv_slider_set_range(brightness_slider, 1, 100);
    lv_slider_set_value(brightness_slider, settings_.brightness_percent, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, brightness_event, LV_EVENT_VALUE_CHANGED, this);

    auto* duration_row = create_row(root_, "App duration");
    duration_label_ = lv_label_create(duration_row);
    lv_obj_set_style_text_color(duration_label_, lv_color_hex(0x8EA0B8), 0);
    auto* duration_slider = lv_slider_create(duration_row);
    lv_obj_set_width(duration_slider, LV_PCT(100));
    lv_slider_set_range(duration_slider, 5, 300);
    lv_slider_set_value(duration_slider,
                        static_cast<std::int32_t>(requested_duration_.load()), LV_ANIM_OFF);
    lv_obj_add_event_cb(duration_slider, duration_event, LV_EVENT_VALUE_CHANGED, this);

    auto* schedule_row = create_row(root_, "Clock only 22:00-07:00");
    auto* schedule_switch = lv_switch_create(schedule_row);
    if (requested_night_clock_.load()) {
        lv_obj_add_state(schedule_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(schedule_switch, night_clock_event, LV_EVENT_VALUE_CHANGED, this);
    update_value_labels();
}

void SettingsApp::on_exit() {
    const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1'000);
    apply_pending(now_ms);
    persist_if_due(now_ms, true);
    if (root_ != nullptr) {
        lv_obj_delete(root_);
    }
    root_ = nullptr;
    brightness_label_ = nullptr;
    duration_label_ = nullptr;
}

void SettingsApp::on_tick(const std::uint64_t now_ms) {
    apply_pending(now_ms);
    persist_if_due(now_ms, false);
}

void SettingsApp::auto_rotate_event(lv_event_t* event) {
    auto* app = event_app(event);
    app->requested_auto_rotate_.store(event_checked(event));
    app->pending_.fetch_or(auto_rotate_changed);
}

void SettingsApp::brightness_event(lv_event_t* event) {
    auto* app = event_app(event);
    app->requested_brightness_.store(static_cast<unsigned>(lv_slider_get_value(
        static_cast<lv_obj_t*>(lv_event_get_target(event)))));
    app->pending_.fetch_or(brightness_changed);
}

void SettingsApp::duration_event(lv_event_t* event) {
    auto* app = event_app(event);
    app->requested_duration_.store(static_cast<unsigned>(lv_slider_get_value(
        static_cast<lv_obj_t*>(lv_event_get_target(event)))));
    app->pending_.fetch_or(duration_changed);
}

void SettingsApp::night_clock_event(lv_event_t* event) {
    auto* app = event_app(event);
    app->requested_night_clock_.store(event_checked(event));
    app->pending_.fetch_or(night_clock_changed);
}

void SettingsApp::rotation_event(lv_event_t* event) {
    auto* app = event_app(event);
    const auto selected =
        lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    if (selected < std::size(kRotations)) {
        app->requested_rotation_.store(kRotations[selected]);
        app->pending_.fetch_or(rotation_changed);
    }
}

void SettingsApp::apply_pending(const std::uint64_t now_ms) noexcept {
    const auto changes = pending_.exchange(0);
    if (changes == 0) {
        return;
    }

    if ((changes & auto_rotate_changed) != 0) {
        settings_.auto_rotate = requested_auto_rotate_.load();
    }
    if ((changes & brightness_changed) != 0) {
        settings_.brightness_percent = static_cast<std::uint8_t>(
            std::min(requested_brightness_.load(), 100U));
        const auto result = board::set_display_brightness(settings_.brightness_percent);
        if (result != ESP_OK) {
            ESP_LOGW(kTag, "Brightness update failed: %s", esp_err_to_name(result));
        }
    }
    if ((changes & duration_changed) != 0) {
        const auto duration = std::clamp(requested_duration_.load(), 5U, 300U);
        for (const char* id : {"clock", "system_info"}) {
            if (auto* preference = find_app_preference(settings_, id); preference != nullptr) {
                preference->duration_seconds = duration;
            }
        }
    }
    if ((changes & rotation_changed) != 0) {
        // Vòng lặp chính thấy rotation_degrees đổi và xoay màn hình.
        settings_.rotation_degrees = static_cast<std::uint16_t>(requested_rotation_.load());
    }
    if ((changes & night_clock_changed) != 0 && settings_.schedule_rule_count > 0) {
        settings_.schedule_rules[0].enabled = requested_night_clock_.load();
    }

    dirty_ = true;
    last_change_ms_ = now_ms;
    update_value_labels();
}

void SettingsApp::persist_if_due(const std::uint64_t now_ms, const bool force) noexcept {
    if (!dirty_ || (!force && now_ms - last_change_ms_ < 1'000)) {
        return;
    }
    const auto result = store_.save(settings_);
    if (result == ESP_OK) {
        dirty_ = false;
    } else {
        ESP_LOGE(kTag, "Failed to save settings: %s", esp_err_to_name(result));
    }
}

void SettingsApp::update_value_labels() noexcept {
    if (brightness_label_ != nullptr) {
        char text[24]{};
        std::snprintf(text, sizeof(text), "%u%%",
                      static_cast<unsigned>(requested_brightness_.load()));
        lv_label_set_text(brightness_label_, text);
    }
    if (duration_label_ != nullptr) {
        char text[24]{};
        std::snprintf(text, sizeof(text), "%u seconds",
                      static_cast<unsigned>(requested_duration_.load()));
        lv_label_set_text(duration_label_, text);
    }
}

}  // namespace apphub::apps
