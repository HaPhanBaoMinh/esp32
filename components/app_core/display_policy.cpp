#include "app_core/display_policy.hpp"

#include <algorithm>
#include <string_view>

namespace apphub {

void DisplayPolicy::pin(App* app, const std::uint64_t until_ms) noexcept {
    pinned_ = app;
    pin_until_ms_ = until_ms;
}

void DisplayPolicy::clear_pin() noexcept {
    pinned_ = nullptr;
    pin_until_ms_ = 0;
}

bool DisplayPolicy::is_rotation_enabled(const Settings& settings,
                                        const App& app) const noexcept {
    const auto& metadata = app.metadata();
    if (!metadata.rotatable) {
        return false;
    }
    const auto* preference = find_app_preference(settings, metadata.id);
    return preference == nullptr ? metadata.enabled_by_default : preference->enabled;
}

std::uint32_t DisplayPolicy::duration_ms(const Settings& settings,
                                         const App& app) const noexcept {
    const auto* preference = find_app_preference(settings, app.metadata().id);
    const auto seconds = preference == nullptr ? app.metadata().default_duration_seconds
                                               : preference->duration_seconds;
    return std::clamp<std::uint32_t>(seconds, 1U, 86'400U) * 1000U;
}

App* DisplayPolicy::first_rotation_app(const Settings& settings) const noexcept {
    for (std::size_t index = 0; index < registry_.size(); ++index) {
        auto* app = registry_.at(index);
        if (app != nullptr && is_rotation_enabled(settings, *app)) {
            return app;
        }
    }
    return nullptr;
}

App* DisplayPolicy::next_rotation_app(const Settings& settings,
                                      const App& current) const noexcept {
    if (registry_.size() == 0) {
        return nullptr;
    }

    std::size_t current_index = 0;
    for (; current_index < registry_.size(); ++current_index) {
        if (registry_.at(current_index) == &current) {
            break;
        }
    }

    for (std::size_t offset = 1; offset <= registry_.size(); ++offset) {
        auto* candidate = registry_.at((current_index + offset) % registry_.size());
        if (candidate != nullptr && is_rotation_enabled(settings, *candidate)) {
            return candidate;
        }
    }
    return nullptr;
}

App* DisplayPolicy::commit(App* app, const Source source,
                           const std::uint64_t now_ms) noexcept {
    if (app != selected_ || source != source_) {
        selected_ = app;
        selected_since_ms_ = now_ms;
        source_ = source;
    }
    return selected_;
}

App* DisplayPolicy::select(const Settings& settings, const ClockSnapshot& clock,
                           const std::uint64_t now_ms) noexcept {
    if (overlay_ != nullptr) {
        return commit(overlay_, Source::overlay, now_ms);
    }

    const auto scheduled_id = ScheduleEvaluator::forced_app(settings, clock);
    if (!scheduled_id.empty()) {
        if (auto* scheduled = registry_.find(scheduled_id); scheduled != nullptr) {
            return commit(scheduled, Source::schedule, now_ms);
        }
    }

    if (pinned_ != nullptr) {
        if (pin_until_ms_ == 0 || now_ms < pin_until_ms_) {
            return commit(pinned_, Source::pin, now_ms);
        }
        clear_pin();
    }

    if (source_ != Source::rotation || selected_ == nullptr ||
        !is_rotation_enabled(settings, *selected_)) {
        return commit(first_rotation_app(settings), Source::rotation, now_ms);
    }

    if (settings.auto_rotate &&
        now_ms - selected_since_ms_ >= duration_ms(settings, *selected_)) {
        return commit(next_rotation_app(settings, *selected_), Source::rotation, now_ms);
    }

    return selected_;
}

}  // namespace apphub

