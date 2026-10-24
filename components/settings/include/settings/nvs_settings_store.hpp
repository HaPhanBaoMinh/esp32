#pragma once

#include "app_core/settings_model.hpp"
#include "esp_err.h"

namespace apphub {

class NvsSettingsStore {
public:
    esp_err_t initialize() noexcept;
    esp_err_t load(Settings& settings) noexcept;
    esp_err_t save(const Settings& settings) noexcept;
    esp_err_t reset(Settings& settings) noexcept;
};

}  // namespace apphub

