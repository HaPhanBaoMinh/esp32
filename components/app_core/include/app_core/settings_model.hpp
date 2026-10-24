#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace apphub {

constexpr std::size_t kAppIdSize = 24;
constexpr std::size_t kTimezoneSize = 48;
constexpr std::size_t kMaxAppPreferences = 16;
constexpr std::size_t kMaxScheduleRules = 12;

enum class ScheduleMode : std::uint8_t {
    force = 0,
    prefer = 1,
    disable = 2,
};

struct AppPreference {
    std::array<char, kAppIdSize> app_id{};
    bool enabled{true};
    std::uint32_t duration_seconds{60};
};

struct ScheduleRule {
    bool enabled{false};
    std::uint8_t weekday_mask{0};  // bit 0 = Monday, bit 6 = Sunday
    std::uint16_t start_minute{0};
    std::uint16_t end_minute{0};
    ScheduleMode mode{ScheduleMode::force};
    std::array<char, kAppIdSize> app_id{};
};

struct Settings {
    static constexpr std::uint32_t kSchemaVersion = 2;

    std::uint32_t schema_version{kSchemaVersion};
    bool auto_rotate{true};
    std::uint8_t brightness_percent{80};
    // Hướng màn hình theo chiều kim đồng hồ: 0/180 = dọc, 90/270 = ngang.
    std::uint16_t rotation_degrees{0};
    std::array<char, kTimezoneSize> timezone{};
    std::array<AppPreference, kMaxAppPreferences> app_preferences{};
    std::size_t app_preference_count{0};
    std::array<ScheduleRule, kMaxScheduleRules> schedule_rules{};
    std::size_t schedule_rule_count{0};
};

template <std::size_t Size>
void assign_text(std::array<char, Size>& destination, const std::string_view source) noexcept {
    destination.fill('\0');
    const auto length = source.size() < Size - 1 ? source.size() : Size - 1;
    for (std::size_t index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
}

Settings make_default_settings() noexcept;
bool is_valid_rotation(std::uint16_t degrees) noexcept;
bool validate_settings(const Settings& settings) noexcept;
const AppPreference* find_app_preference(const Settings& settings,
                                         std::string_view app_id) noexcept;
AppPreference* find_app_preference(Settings& settings, std::string_view app_id) noexcept;
// Trả về preference sẵn có hoặc thêm mới; nullptr khi danh sách đã đầy.
AppPreference* ensure_app_preference(Settings& settings, std::string_view app_id,
                                     bool enabled, std::uint32_t duration_seconds) noexcept;

}  // namespace apphub

