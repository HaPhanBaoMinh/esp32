#pragma once

#include <cstdint>

#include "app_core/app.hpp"
#include "lvgl.h"

namespace apphub::apps {

class SystemInfoApp final : public App {
public:
    const AppMetadata& metadata() const noexcept override;
    void on_enter() override;
    void on_exit() override;
    void on_tick(std::uint64_t now_ms) override;

private:
    std::uint64_t last_update_ms_{0};
    lv_obj_t* root_{nullptr};
    lv_obj_t* details_label_{nullptr};
};

}  // namespace apphub::apps

