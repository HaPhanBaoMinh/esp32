#pragma once

#include "app_core/app_registry.hpp"
#include "app_core/settings_model.hpp"
#include "esp_err.h"

namespace apphub::web {

// Web cấu hình tại http://<ip>/ hoặc http://apphub.local/. HTTP handler chạy
// trong task riêng nên không chạm trực tiếp vào Settings của vòng lặp chính:
// vòng lặp publish snapshot, web gửi lại bản đã validate qua take_settings_update().
esp_err_t start(const AppRegistry& registry) noexcept;

void publish_settings(const Settings& settings) noexcept;
bool take_settings_update(Settings& settings) noexcept;

}  // namespace apphub::web
