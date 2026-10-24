#include "app_core/schedule.hpp"

#include <algorithm>

namespace apphub {
namespace {

bool weekday_enabled(const std::uint8_t mask, const std::uint8_t weekday) noexcept {
    return weekday < 7 && (mask & (1U << weekday)) != 0;
}

}  // namespace

bool ScheduleEvaluator::matches(const ScheduleRule& rule,
                                const ClockSnapshot& clock) noexcept {
    if (!rule.enabled || !clock.valid || clock.weekday > 6 || clock.minute_of_day >= 1440) {
        return false;
    }

    if (rule.start_minute == rule.end_minute) {
        return weekday_enabled(rule.weekday_mask, clock.weekday);
    }

    if (rule.start_minute < rule.end_minute) {
        return weekday_enabled(rule.weekday_mask, clock.weekday) &&
               clock.minute_of_day >= rule.start_minute &&
               clock.minute_of_day < rule.end_minute;
    }

    // Rule qua nửa đêm: phần sau 00:00 thuộc ngày bắt đầu trước đó.
    if (clock.minute_of_day >= rule.start_minute) {
        return weekday_enabled(rule.weekday_mask, clock.weekday);
    }
    if (clock.minute_of_day < rule.end_minute) {
        const auto previous_weekday = static_cast<std::uint8_t>((clock.weekday + 6) % 7);
        return weekday_enabled(rule.weekday_mask, previous_weekday);
    }
    return false;
}

std::string_view ScheduleEvaluator::forced_app(const Settings& settings,
                                               const ClockSnapshot& clock) noexcept {
    const auto count = std::min(settings.schedule_rule_count,
                                settings.schedule_rules.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& rule = settings.schedule_rules[index];
        if (rule.mode == ScheduleMode::force && matches(rule, clock)) {
            return rule.app_id.data();
        }
    }
    return {};
}

}  // namespace apphub

