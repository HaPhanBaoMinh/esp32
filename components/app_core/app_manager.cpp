#include "app_core/app_manager.hpp"

namespace apphub {

void AppManager::activate(App* app) noexcept {
    if (app == active_) {
        return;
    }

    if (active_ != nullptr) {
        active_->on_exit();
    }

    active_ = app;
    if (active_ != nullptr) {
        active_->on_enter();
    }
}

void AppManager::tick(const std::uint64_t now_ms) noexcept {
    if (active_ != nullptr) {
        active_->on_tick(now_ms);
    }
}

}  // namespace apphub

