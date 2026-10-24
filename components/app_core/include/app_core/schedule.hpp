#pragma once

#include <cstdint>
#include <string_view>

#include "app_core/settings_model.hpp"

namespace apphub {

struct ClockSnapshot {
    bool valid{false};
    std::uint8_t weekday{0};  // Monday = 0, Sunday = 6
    std::uint16_t minute_of_day{0};
};

class ScheduleEvaluator {
public:
    static bool matches(const ScheduleRule& rule, const ClockSnapshot& clock) noexcept;
    static std::string_view forced_app(const Settings& settings,
                                       const ClockSnapshot& clock) noexcept;
};

}  // namespace apphub

