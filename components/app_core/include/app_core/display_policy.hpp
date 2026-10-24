#pragma once

#include <cstdint>

#include "app_core/app_registry.hpp"
#include "app_core/schedule.hpp"

namespace apphub {

class DisplayPolicy {
public:
    explicit DisplayPolicy(const AppRegistry& registry) noexcept : registry_(registry) {}

    App* select(const Settings& settings, const ClockSnapshot& clock,
                std::uint64_t now_ms) noexcept;

    void set_overlay(App* app) noexcept { overlay_ = app; }
    void clear_overlay() noexcept { overlay_ = nullptr; }
    void pin(App* app, std::uint64_t until_ms) noexcept;
    void clear_pin() noexcept;

private:
    enum class Source : std::uint8_t { none, rotation, pin, schedule, overlay };

    bool is_rotation_enabled(const Settings& settings, const App& app) const noexcept;
    std::uint32_t duration_ms(const Settings& settings, const App& app) const noexcept;
    App* first_rotation_app(const Settings& settings) const noexcept;
    App* next_rotation_app(const Settings& settings, const App& current) const noexcept;
    App* commit(App* app, Source source, std::uint64_t now_ms) noexcept;

    const AppRegistry& registry_;
    App* selected_{nullptr};
    App* overlay_{nullptr};
    App* pinned_{nullptr};
    std::uint64_t pin_until_ms_{0};
    std::uint64_t selected_since_ms_{0};
    Source source_{Source::none};
};

}  // namespace apphub

