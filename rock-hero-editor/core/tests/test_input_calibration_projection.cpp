#include "input_calibration/input_calibration_projection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/fake_live_input.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>

namespace rock_hero::editor::core
{

namespace
{

using common::audio::testing::makeInputDeviceIdentity;

constexpr common::audio::LiveInputMonitoringContext g_ready{.session_ready = true};

// A monitor over fakes whose current route is optionally calibrated, refreshed once.
struct Harness
{
    explicit Harness(std::optional<double> stored_gain_db)
    {
        devices.current_input_identity = route;
        if (stored_gain_db.has_value())
        {
            store.input_calibrations.push_back(
                common::audio::InputCalibrationState{
                    .calibration_gain = common::audio::Gain{*stored_gain_db},
                    .input_device_identity = route,
                });
        }
        monitor.refresh(g_ready);
    }

    common::audio::InputDeviceIdentity route{makeInputDeviceIdentity()};
    common::audio::testing::FakeLiveInput live_input;
    common::audio::testing::ConfigurableAudioDeviceConfiguration devices;
    common::audio::testing::InMemoryAudioConfigStore store;
    common::audio::LiveInputMonitor monitor{live_input, devices, store};
};

} // namespace

// The status follows the route and its stored calibration, and a refused route reads as
// unavailable.
TEST_CASE(
    "Input calibration status follows the route and its calibration", "[core][input-calibration]")
{
    const Harness calibrated{5.0};
    CHECK(inputCalibrationStatusFor(calibrated.monitor) == InputCalibrationStatus::Calibrated);

    Harness uncalibrated{std::nullopt};
    CHECK(
        inputCalibrationStatusFor(uncalibrated.monitor) ==
        InputCalibrationStatus::MissingCalibration);

    uncalibrated.devices.current_input_identity.reset();
    uncalibrated.monitor.refresh(g_ready);
    CHECK(
        inputCalibrationStatusFor(uncalibrated.monitor) ==
        InputCalibrationStatus::NoActiveInputDevice);

    Harness refused{5.0};
    refused.live_input.next_set_input_gain_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable, "route gone"
    };
    refused.monitor.refresh(g_ready);
    CHECK(inputCalibrationStatusFor(refused.monitor) == InputCalibrationStatus::Unavailable);
}

// A calibrated route with no editor window open is auditionable.
TEST_CASE(
    "Input calibration projection builds an active calibrated projection",
    "[core][input-calibration]")
{
    const Harness harness{5.0};

    const InputCalibrationProjection projection =
        makeInputCalibrationProjection(harness.monitor, std::nullopt, false);

    CHECK(projection.status == InputCalibrationStatus::Calibrated);
    CHECK(projection.calibrate_enabled);
    CHECK(projection.audio_device_settings_enabled);
    CHECK(projection.disabled_message.empty());
    CHECK_FALSE(projection.prompt.has_value());
}

// The settings window holds the route: the route still shows as calibrated, but neither window
// may open over it.
TEST_CASE(
    "Input calibration projection keeps calibrated status while settings are open",
    "[core][input-calibration]")
{
    const Harness harness{5.0};

    const InputCalibrationProjection projection =
        makeInputCalibrationProjection(harness.monitor, std::nullopt, true);

    CHECK(projection.status == InputCalibrationStatus::Calibrated);
    CHECK_FALSE(projection.audio_device_settings_enabled);
    CHECK_FALSE(projection.calibrate_enabled);
}

// An open prompt names its route and carries the route's stored gain, absent while uncalibrated.
TEST_CASE(
    "Input calibration projection projects the prompt with the stored gain",
    "[core][input-calibration]")
{
    const Harness uncalibrated{std::nullopt};
    const InputCalibrationProjection fresh =
        makeInputCalibrationProjection(uncalibrated.monitor, uncalibrated.route, false);
    REQUIRE(fresh.prompt.has_value());
    if (fresh.prompt.has_value())
    {
        CHECK(fresh.prompt->route == uncalibrated.route);
        CHECK_FALSE(fresh.prompt->stored_gain_db.has_value());
    }
    CHECK(fresh.status == InputCalibrationStatus::MissingCalibration);
    CHECK_FALSE(fresh.audio_device_settings_enabled);

    const Harness calibrated{3.1};
    const InputCalibrationProjection stored =
        makeInputCalibrationProjection(calibrated.monitor, calibrated.route, false);
    REQUIRE(stored.prompt.has_value());
    if (stored.prompt.has_value())
    {
        CHECK(stored.prompt->stored_gain_db == std::optional{3.1});
    }
}

} // namespace rock_hero::editor::core
