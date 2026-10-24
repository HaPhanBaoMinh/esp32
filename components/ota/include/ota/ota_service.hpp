#pragma once

#include "esp_err.h"

namespace apphub {

class OtaService {
public:
    esp_err_t confirm_running_image() noexcept;
    esp_err_t install_from_https(const char* url) noexcept;
};

}  // namespace apphub

