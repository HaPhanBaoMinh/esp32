#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string_view>

#include "app_core/app_manager.hpp"
#include "app_core/app_registry.hpp"
#include "app_core/display_policy.hpp"
#include "apps/clock_app.hpp"
#include "apps/settings_app.hpp"
#include "apps/system_info_app.hpp"
#include "board/board.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/network_service.hpp"
#include "ota/ota_service.hpp"
#include "settings/nvs_settings_store.hpp"
#include "ui/ui_shell.hpp"
#include "web/web_server.hpp"

namespace {

constexpr char kTag[] = "app_hub";

apphub::ClockSnapshot current_clock() noexcept {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);

    // tm_wday: Sunday=0. App Hub: Monday=0.
    const auto weekday = static_cast<std::uint8_t>((local.tm_wday + 6) % 7);
    return {
        .valid = local.tm_year + 1900 >= 2024,
        .weekday = weekday,
        .minute_of_day = static_cast<std::uint16_t>(local.tm_hour * 60 + local.tm_min),
    };
}

bool rotation_enabled(const apphub::Settings& settings, const apphub::App& app) noexcept {
    if (!app.metadata().rotatable) {
        return false;
    }
    const auto* preference = apphub::find_app_preference(settings, app.metadata().id);
    return preference == nullptr ? app.metadata().enabled_by_default : preference->enabled;
}

apphub::App* adjacent_app(const apphub::AppRegistry& registry,
                          const apphub::Settings& settings, const apphub::App* active,
                          const int direction) noexcept {
    if (registry.size() == 0 || direction == 0) {
        return nullptr;
    }
    std::size_t current = 0;
    for (; current < registry.size(); ++current) {
        if (registry.at(current) == active) {
            break;
        }
    }
    if (current == registry.size()) {
        current = direction > 0 ? registry.size() - 1 : 0;
    }

    for (std::size_t offset = 1; offset <= registry.size(); ++offset) {
        const auto candidate_index = direction > 0
                                         ? (current + offset) % registry.size()
                                         : (current + registry.size() -
                                            (offset % registry.size())) % registry.size();
        auto* candidate = registry.at(candidate_index);
        if (candidate != nullptr && rotation_enabled(settings, *candidate)) {
            return candidate;
        }
    }
    return nullptr;
}

void apply_timezone(const apphub::Settings& settings) noexcept {
    setenv("TZ", settings.timezone.data(), 1);
    tzset();
}

// Settings mới từ web: áp các giá trị có hiệu lực ngay rồi lưu NVS. Rotation
// được vòng lặp chính áp khi đang giữ LVGL lock.
void apply_web_update(apphub::Settings& settings, const apphub::Settings& update,
                      apphub::NvsSettingsStore& store) noexcept {
    const bool brightness_changed = update.brightness_percent != settings.brightness_percent;
    const bool timezone_changed =
        std::string_view(update.timezone.data()) != settings.timezone.data();
    settings = update;
    if (brightness_changed) {
        apphub::board::set_display_brightness(settings.brightness_percent);
    }
    if (timezone_changed) {
        apply_timezone(settings);
    }
    const auto result = store.save(settings);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Saving web settings failed: %s", esp_err_to_name(result));
    } else {
        ESP_LOGI(kTag, "Settings updated from web");
    }
}

}  // namespace

extern "C" void app_main() {
    apphub::NvsSettingsStore settings_store;
    ESP_ERROR_CHECK(settings_store.initialize());

    // static: Settings khá lớn (~1 KB), không nên nằm trên stack main task.
    static apphub::Settings settings{};
    static apphub::Settings web_update{};
    ESP_ERROR_CHECK(settings_store.load(settings));
    apply_timezone(settings);

    if (const auto result = apphub::network::start(); result != ESP_OK) {
        ESP_LOGE(kTag, "Network start failed: %s", esp_err_to_name(result));
    }

    const auto board_info = apphub::board::probe();
    ESP_LOGI(kTag, "ESP32-S3 rev=%u flash=%uMB psram=%uMB",
             static_cast<unsigned>(board_info.chip_revision),
             static_cast<unsigned>(board_info.flash_bytes / (1024 * 1024)),
             static_cast<unsigned>(board_info.psram_bytes / (1024 * 1024)));

    const auto display_result =
        apphub::board::initialize_display_and_touch(settings.brightness_percent);
    if (display_result != ESP_OK) {
        ESP_LOGW(kTag, "Running framework in headless mode: board init failed");
    }
    const auto ui_result = display_result == ESP_OK ? apphub::ui::initialize()
                                                    : ESP_ERR_INVALID_STATE;
    if (ui_result != ESP_OK) {
        ESP_LOGW(kTag, "LVGL UI unavailable: %s", esp_err_to_name(ui_result));
    }

    apphub::apps::ClockApp clock_app;
    apphub::apps::SystemInfoApp system_info_app;
    apphub::apps::SettingsApp settings_app(settings, settings_store);
    apphub::AppRegistry registry;
    ESP_ERROR_CHECK(registry.add(clock_app) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(registry.add(system_info_app) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(registry.add(settings_app) ? ESP_OK : ESP_FAIL);

    apphub::AppManager manager;
    apphub::DisplayPolicy policy(registry);

    apphub::web::publish_settings(settings);
    if (const auto result = apphub::web::start(registry); result != ESP_OK) {
        ESP_LOGE(kTag, "Web server start failed: %s", esp_err_to_name(result));
    }

    // Chỉ xác nhận OTA sau khi NVS, app framework và display/touch đã tự kiểm
    // tra xong. Nếu board variant sai, lần khởi động kế tiếp sẽ rollback.
    apphub::OtaService ota;
    if (display_result == ESP_OK) {
        ESP_ERROR_CHECK(ota.confirm_running_image());
    } else {
        ESP_LOGW(kTag, "OTA image is not confirmed while display is unavailable");
    }

    apphub::board::TouchPoint last_touch{};
    bool settings_open = false;
    std::uint16_t applied_rotation = 0;
    while (true) {
        const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);

        if (apphub::web::take_settings_update(web_update)) {
            apply_web_update(settings, web_update, settings_store);
        }

        if (apphub::ui::is_ready() && apphub::ui::take_settings_request()) {
            settings_open = !settings_open;
            if (settings_open) {
                policy.set_overlay(&settings_app);
            } else {
                policy.clear_overlay();
            }
        }

        if (apphub::ui::is_ready() && !settings_open) {
            const auto navigation = apphub::ui::take_navigation_request();
            if (navigation != 0) {
                if (auto* target = adjacent_app(registry, settings, manager.active(), navigation);
                    target != nullptr) {
                    policy.pin(target, now_ms + 60'000);
                }
            }
        }

        if (!apphub::ui::is_ready() || apphub::ui::lock(50)) {
            // Rotation có thể đổi từ web hoặc Settings app; layout shell tự
            // co giãn nên app đang mở không cần tạo lại view.
            if (apphub::ui::is_ready() && applied_rotation != settings.rotation_degrees) {
                apphub::ui::set_rotation(settings.rotation_degrees);
                applied_rotation = settings.rotation_degrees;
            }
            manager.activate(policy.select(settings, current_clock(), now_ms));
            manager.tick(now_ms);
            if (apphub::ui::is_ready()) {
                apphub::ui::unlock();
            }
        }
        apphub::web::publish_settings(settings);

        if (!apphub::ui::is_ready()) {
            apphub::board::TouchPoint touch{};
            if (apphub::board::read_touch(touch) &&
                (touch.x != last_touch.x || touch.y != last_touch.y)) {
                ESP_LOGI(kTag, "touch x=%u y=%u strength=%u",
                         static_cast<unsigned>(touch.x), static_cast<unsigned>(touch.y),
                         static_cast<unsigned>(touch.strength));
                last_touch = touch;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

