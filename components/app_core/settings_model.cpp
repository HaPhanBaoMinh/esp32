#include "app_core/settings_model.hpp"

#include <algorithm>

namespace apphub {
namespace {

template <std::size_t Size>
bool is_valid_text(const std::array<char, Size>& value, const bool allow_empty) noexcept {
    const auto terminator = std::find(value.begin(), value.end(), '\0');
    return terminator != value.end() && (allow_empty || terminator != value.begin());
}

}  // namespace

Settings make_default_settings() noexcept {
    Settings settings{};
    assign_text(settings.timezone, "ICT-7");
    ensure_app_preference(settings, "clock", true, 60);
    ensure_app_preference(settings, "system_info", true, 60);

    auto& night_clock = settings.schedule_rules[settings.schedule_rule_count++];
    night_clock.enabled = true;
    night_clock.weekday_mask = 0x7F;
    night_clock.start_minute = 22 * 60;
    night_clock.end_minute = 7 * 60;
    night_clock.mode = ScheduleMode::force;
    assign_text(night_clock.app_id, "clock");

    return settings;
}

bool is_valid_rotation(const std::uint16_t degrees) noexcept {
    return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

bool validate_settings(const Settings& settings) noexcept {
    if (settings.schema_version != Settings::kSchemaVersion ||
        settings.brightness_percent > 100 ||
        !is_valid_rotation(settings.rotation_degrees) ||
        !is_valid_text(settings.timezone, false) ||
        settings.app_preference_count > settings.app_preferences.size() ||
        settings.schedule_rule_count > settings.schedule_rules.size()) {
        return false;
    }

    for (std::size_t index = 0; index < settings.app_preference_count; ++index) {
        const auto& preference = settings.app_preferences[index];
        if (!is_valid_text(preference.app_id, false) || preference.duration_seconds == 0 ||
            preference.duration_seconds > 86'400) {
            return false;
        }
    }

    for (std::size_t index = 0; index < settings.schedule_rule_count; ++index) {
        const auto& rule = settings.schedule_rules[index];
        if ((rule.weekday_mask & 0x80U) != 0 || rule.start_minute >= 1'440 ||
            rule.end_minute >= 1'440 ||
            static_cast<std::uint8_t>(rule.mode) >
                static_cast<std::uint8_t>(ScheduleMode::disable) ||
            !is_valid_text(rule.app_id, false)) {
            return false;
        }
    }

    return true;
}

const AppPreference* find_app_preference(const Settings& settings,
                                         const std::string_view app_id) noexcept {
    const auto count = std::min(settings.app_preference_count,
                                settings.app_preferences.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (app_id == settings.app_preferences[index].app_id.data()) {
            return &settings.app_preferences[index];
        }
    }
    return nullptr;
}

AppPreference* find_app_preference(Settings& settings, const std::string_view app_id) noexcept {
    return const_cast<AppPreference*>(find_app_preference(
        static_cast<const Settings&>(settings), app_id));
}

AppPreference* ensure_app_preference(Settings& settings, const std::string_view app_id,
                                     const bool enabled,
                                     const std::uint32_t duration_seconds) noexcept {
    if (auto* existing = find_app_preference(settings, app_id); existing != nullptr) {
        return existing;
    }
    if (app_id.empty() || app_id.size() >= kAppIdSize ||
        settings.app_preference_count >= settings.app_preferences.size()) {
        return nullptr;
    }
    auto& preference = settings.app_preferences[settings.app_preference_count++];
    assign_text(preference.app_id, app_id);
    preference.enabled = enabled;
    preference.duration_seconds = duration_seconds;
    return &preference;
}

}  // namespace apphub

