#include "app_core/app_registry.hpp"

namespace apphub {

bool AppRegistry::add(App& app) noexcept {
    const auto& metadata = app.metadata();
    if (metadata.id == nullptr || metadata.id[0] == '\0' || size_ >= apps_.size() ||
        find(metadata.id) != nullptr) {
        return false;
    }

    apps_[size_++] = &app;
    return true;
}

App* AppRegistry::find(const std::string_view id) const noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        if (id == apps_[index]->metadata().id) {
            return apps_[index];
        }
    }
    return nullptr;
}

App* AppRegistry::at(const std::size_t index) const noexcept {
    return index < size_ ? apps_[index] : nullptr;
}

}  // namespace apphub

