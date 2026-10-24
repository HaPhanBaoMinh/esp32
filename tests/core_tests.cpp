#include <cassert>
#include <cstdint>

#include "app_core/app_manager.hpp"
#include "app_core/app_registry.hpp"
#include "app_core/display_policy.hpp"
#include "app_core/schedule.hpp"
#include "app_core/settings_model.hpp"

namespace {

class FakeApp final : public apphub::App {
public:
    explicit FakeApp(const apphub::AppMetadata metadata) : metadata_(metadata) {}

    const apphub::AppMetadata& metadata() const noexcept override { return metadata_; }
    void on_enter() override { ++enter_count; }
    void on_exit() override { ++exit_count; }
    void on_tick(std::uint64_t) override { ++tick_count; }

    int enter_count{0};
    int exit_count{0};
    int tick_count{0};

private:
    apphub::AppMetadata metadata_;
};

constexpr apphub::AppMetadata kClock{"clock", "Clock", true, true, 60};
constexpr apphub::AppMetadata kInfo{"system_info", "System Info", true, true, 60};
constexpr apphub::AppMetadata kSettings{"settings", "Settings", false, false, 60};

void test_registry_and_lifecycle() {
    FakeApp clock(kClock);
    FakeApp info(kInfo);
    apphub::AppRegistry registry;
    assert(registry.add(clock));
    assert(registry.add(info));
    assert(!registry.add(clock));
    assert(registry.find("clock") == &clock);

    apphub::AppManager manager;
    manager.activate(&clock);
    manager.tick(1);
    manager.activate(&info);
    assert(clock.enter_count == 1);
    assert(clock.exit_count == 1);
    assert(clock.tick_count == 1);
    assert(info.enter_count == 1);

    manager.activate(nullptr);
    assert(info.exit_count == 1);
    assert(manager.active() == nullptr);
}

void test_settings_validation() {
    auto settings = apphub::make_default_settings();
    assert(apphub::validate_settings(settings));

    settings.brightness_percent = 101;
    assert(!apphub::validate_settings(settings));

    settings = apphub::make_default_settings();
    settings.app_preferences[0].app_id.fill('x');
    assert(!apphub::validate_settings(settings));

    settings = apphub::make_default_settings();
    settings.schedule_rules[0].start_minute = 1'440;
    assert(!apphub::validate_settings(settings));

    settings = apphub::make_default_settings();
    settings.rotation_degrees = 90;
    assert(apphub::validate_settings(settings));
    settings.rotation_degrees = 45;
    assert(!apphub::validate_settings(settings));

    settings = apphub::make_default_settings();
    const auto count = settings.app_preference_count;
    assert(apphub::ensure_app_preference(settings, "clock", false, 5) ==
           apphub::find_app_preference(settings, "clock"));
    assert(settings.app_preference_count == count);
    auto* added = apphub::ensure_app_preference(settings, "gold_price", false, 30);
    assert(added != nullptr && !added->enabled && added->duration_seconds == 30);
    assert(settings.app_preference_count == count + 1);
    assert(apphub::ensure_app_preference(settings, "this_id_is_way_too_long_for_it", true,
                                         60) == nullptr);
}

void test_rotation_and_overlay() {
    FakeApp clock(kClock);
    FakeApp info(kInfo);
    FakeApp settings_app(kSettings);
    apphub::AppRegistry registry;
    assert(registry.add(clock));
    assert(registry.add(info));
    assert(registry.add(settings_app));

    auto settings = apphub::make_default_settings();
    settings.schedule_rules[0].enabled = false;
    apphub::DisplayPolicy policy(registry);
    const apphub::ClockSnapshot no_clock{};

    assert(policy.select(settings, no_clock, 0) == &clock);
    assert(policy.select(settings, no_clock, 59'999) == &clock);
    assert(policy.select(settings, no_clock, 60'000) == &info);

    policy.set_overlay(&settings_app);
    assert(policy.select(settings, no_clock, 61'000) == &settings_app);
    policy.clear_overlay();
    assert(policy.select(settings, no_clock, 62'000) == &clock);
}

void test_overnight_schedule_and_priority() {
    FakeApp clock(kClock);
    FakeApp info(kInfo);
    apphub::AppRegistry registry;
    assert(registry.add(clock));
    assert(registry.add(info));

    auto settings = apphub::make_default_settings();
    auto& rule = settings.schedule_rules[0];
    rule.weekday_mask = 1U << 4;  // Friday only

    const apphub::ClockSnapshot friday_late{true, 4, 23 * 60};
    const apphub::ClockSnapshot saturday_early{true, 5, 2 * 60};
    const apphub::ClockSnapshot saturday_late{true, 5, 8 * 60};
    assert(apphub::ScheduleEvaluator::matches(rule, friday_late));
    assert(apphub::ScheduleEvaluator::matches(rule, saturday_early));
    assert(!apphub::ScheduleEvaluator::matches(rule, saturday_late));

    apphub::DisplayPolicy policy(registry);
    policy.pin(&info, 1'000'000);
    assert(policy.select(settings, friday_late, 100) == &clock);
    assert(policy.select(settings, saturday_late, 200) == &info);
}

}  // namespace

int main() {
    test_registry_and_lifecycle();
    test_rotation_and_overlay();
    test_overnight_schedule_and_priority();
    test_settings_validation();
    return 0;
}

