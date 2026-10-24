#include "settings/nvs_settings_store.hpp"

#include <cstddef>
#include <cstdint>

#include "nvs.h"
#include "nvs_flash.h"

namespace apphub {
namespace {

constexpr char kNamespace[] = "app_hub";
constexpr char kSettingsKey[] = "settings";

}  // namespace

esp_err_t NvsSettingsStore::initialize() noexcept {
    auto result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result == ESP_OK) {
            result = nvs_flash_init();
        }
    }
    return result;
}

esp_err_t NvsSettingsStore::load(Settings& settings) noexcept {
    nvs_handle_t handle{};
    auto result = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        settings = make_default_settings();
        return save(settings);
    }
    if (result != ESP_OK) {
        return result;
    }

    std::size_t size = sizeof(settings);
    result = nvs_get_blob(handle, kSettingsKey, &settings, &size);
    nvs_close(handle);

    if (result != ESP_OK || size != sizeof(settings) || !validate_settings(settings)) {
        settings = make_default_settings();
        return save(settings);
    }
    return ESP_OK;
}

esp_err_t NvsSettingsStore::save(const Settings& settings) noexcept {
    nvs_handle_t handle{};
    auto result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_set_blob(handle, kSettingsKey, &settings, sizeof(settings));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

esp_err_t NvsSettingsStore::reset(Settings& settings) noexcept {
    settings = make_default_settings();
    return save(settings);
}

}  // namespace apphub

