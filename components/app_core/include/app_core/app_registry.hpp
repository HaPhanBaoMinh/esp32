#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "app_core/app.hpp"

namespace apphub {

class AppRegistry {
public:
    static constexpr std::size_t kMaxApps = 16;

    bool add(App& app) noexcept;
    App* find(std::string_view id) const noexcept;
    App* at(std::size_t index) const noexcept;
    std::size_t size() const noexcept { return size_; }

private:
    std::array<App*, kMaxApps> apps_{};
    std::size_t size_{0};
};

}  // namespace apphub

