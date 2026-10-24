#include "ota/ota_service.hpp"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"

namespace apphub {

esp_err_t OtaService::confirm_running_image() noexcept {
    const auto* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    const auto result = esp_ota_get_state_partition(running, &state);
    if (result == ESP_ERR_NOT_SUPPORTED || result == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (result != ESP_OK) {
        return result;
    }
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        return esp_ota_mark_app_valid_cancel_rollback();
    }
    return ESP_OK;
}

esp_err_t OtaService::install_from_https(const char* url) noexcept {
    if (url == nullptr || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t http_config{};
    http_config.url = url;
    http_config.timeout_ms = 30'000;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.keep_alive_enable = true;

    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;
    return esp_https_ota(&ota_config);
}

}  // namespace apphub

