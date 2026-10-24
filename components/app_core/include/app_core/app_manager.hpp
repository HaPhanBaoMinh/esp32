#pragma once

#include <cstdint>

#include "app_core/app.hpp"

namespace apphub {

class AppManager {
public:
    void activate(App* app) noexcept;
    void tick(std::uint64_t now_ms) noexcept;

    App* active() const noexcept { return active_; }

private:
    App* active_{nullptr};
};

}  // namespace apphub

