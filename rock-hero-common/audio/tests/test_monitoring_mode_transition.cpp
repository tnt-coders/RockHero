#include "monitoring_mode_transition.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::common::audio
{

// Enabling one mode must switch the other off, because the two are mutually exclusive.
TEST_CASE("Enabling live input monitoring turns calibration off", "[audio][monitoring-mode]")
{
    const std::optional<MonitoringFlags> result = monitoringFlagsForRequest(
        MonitoringFlags{.live_input = false, .calibration = true},
        MonitorChannel::LiveInput,
        true,
        true);

    REQUIRE(result.has_value());
    if (result.has_value())
    {
        REQUIRE(result->live_input);
        REQUIRE_FALSE(result->calibration);
    }
}

// Enabling calibration mode must enforce the same mutual-exclusion rule.
TEST_CASE("Enabling calibration monitoring turns live input off", "[audio][monitoring-mode]")
{
    const std::optional<MonitoringFlags> result = monitoringFlagsForRequest(
        MonitoringFlags{.live_input = true, .calibration = false},
        MonitorChannel::Calibration,
        true,
        true);

    REQUIRE(result.has_value());
    if (result.has_value())
    {
        REQUIRE_FALSE(result->live_input);
        REQUIRE(result->calibration);
    }
}

// Disabling a mode is a one-sided change: the other mode keeps whatever state it had.
TEST_CASE("Disabling a monitoring mode leaves the other untouched", "[audio][monitoring-mode]")
{
    const std::optional<MonitoringFlags> disabled_live = monitoringFlagsForRequest(
        MonitoringFlags{.live_input = false, .calibration = true},
        MonitorChannel::LiveInput,
        false,
        true);

    REQUIRE(disabled_live.has_value());
    if (disabled_live.has_value())
    {
        REQUIRE_FALSE(disabled_live->live_input);
        REQUIRE(disabled_live->calibration);
    }
}

// Enabling routes the live input, which is impossible without an input device to route from.
TEST_CASE("Enabling monitoring without an input device is rejected", "[audio][monitoring-mode]")
{
    REQUIRE_FALSE(
        monitoringFlagsForRequest(MonitoringFlags{}, MonitorChannel::LiveInput, true, false)
            .has_value());
    REQUIRE_FALSE(
        monitoringFlagsForRequest(MonitoringFlags{}, MonitorChannel::Calibration, true, false)
            .has_value());
}

// Disabling never routes anything, so it stays valid even when no input device is available.
TEST_CASE("Disabling monitoring without an input device still applies", "[audio][monitoring-mode]")
{
    const std::optional<MonitoringFlags> result = monitoringFlagsForRequest(
        MonitoringFlags{.live_input = true, .calibration = false},
        MonitorChannel::LiveInput,
        false,
        false);

    REQUIRE(result.has_value());
    if (result.has_value())
    {
        REQUIRE_FALSE(result->live_input);
        REQUIRE_FALSE(result->calibration);
    }
}

} // namespace rock_hero::common::audio
