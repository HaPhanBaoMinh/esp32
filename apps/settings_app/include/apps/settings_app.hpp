#pragma once

#include <atomic>
#include <cstdint>

#include "app_core/app.hpp"
#include "app_core/settings_model.hpp"
#include "lvgl.h"
#include "settings/nvs_settings_store.hpp"

namespace apphub::apps {

class SettingsApp final : public App {
public:
    SettingsApp(Settings& settings, NvsSettingsStore& store) noexcept
        : settings_(settings), store_(store) {}

    const AppMetadata& metadata() const noexcept override;
    void on_enter() override;
    void on_exit() override;
    void on_tick(std::uint64_t now_ms) override;

private:
    enum Pending : std::uint32_t {
        auto_rotate_changed = 1U << 0U,
        brightness_changed = 1U << 1U,
        duration_changed = 1U << 2U,
        night_clock_changed = 1U << 3U,
        rotation_changed = 1U << 4U,
    };

    static void auto_rotate_event(lv_event_t* event);
    static void brightness_event(lv_event_t* event);
    static void duration_event(lv_event_t* event);
    static void night_clock_event(lv_event_t* event);
    static void rotation_event(lv_event_t* event);

    void apply_pending(std::uint64_t now_ms) noexcept;
    void persist_if_due(std::uint64_t now_ms, bool force) noexcept;
    void update_value_labels() noexcept;

    Settings& settings_;
    NvsSettingsStore& store_;
    std::atomic<std::uint32_t> pending_{0};
    std::atomic_bool requested_auto_rotate_{true};
    std::atomic_uint requested_brightness_{80};
    std::atomic_uint requested_duration_{60};
    std::atomic_bool requested_night_clock_{true};
    std::atomic_uint requested_rotation_{0};
    bool dirty_{false};
    std::uint64_t last_change_ms_{0};
    lv_obj_t* root_{nullptr};
    lv_obj_t* brightness_label_{nullptr};
    lv_obj_t* duration_label_{nullptr};
};

}  // namespace apphub::apps
