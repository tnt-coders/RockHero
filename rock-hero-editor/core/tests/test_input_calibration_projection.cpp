#include "input_calibration/input_calibration_projection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/fake_live_input.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Builds a physical input identity with defaults that represent one stable ASIO route.
[[nodiscard]] common::audio::InputDeviceIdentity makeIdentity(
    std::string backend_name = "ASIO", std::string input_device_name = "Interface A",
    int input_channel_index = 0, std::string input_channel_name = "Input 1")
{
    return common::audio::InputDeviceIdentity{
        .backend_name = std::move(backend_name),
        .input_device_name = std::move(input_device_name),
        .input_channel_index = input_channel_index,
        .input_channel_name = std::move(input_channel_name),
    };
}

// Builds a saved calibration state for a physical route.
[[nodiscard]] common::audio::InputCalibrationState calibrationFor(
    const common::audio::InputDeviceIdentity& identity, double gain_db)
{
    return common::audio::InputCalibrationState{
        .calibration_gain = common::audio::Gain{gain_db},
        .input_device_identity = identity,
    };
}

// Session context where arrangement audio is loaded and live input may be calibrated.
constexpr common::audio::LiveInputMonitoringContext g_ready{
    .live_input_ready = true, .arrangement_loaded = true
};

} // namespace

// Each monitoring reason projects to a fixed signal-chain status. This pins the enum mapping the
// projection module owns after the calibration workflow moved to common/audio.
TEST_CASE(
    "Input calibration projection maps each monitoring reason to status", "[core][projection]")
{
    struct Case
    {
        common::audio::MonitoringDisabledReason reason;
        bool backend_available;
        InputCalibrationStatus expected_status;
    };

    const Case cases[] = {
        {common::audio::MonitoringDisabledReason::None, true, InputCalibrationStatus::Calibrated},
        {common::audio::MonitoringDisabledReason::None, false, InputCalibrationStatus::Unavailable},
        {common::audio::MonitoringDisabledReason::AudioDeviceSettingsOpen,
         true,
         InputCalibrationStatus::NoActiveInputDevice},
        {common::audio::MonitoringDisabledReason::SessionNotReady,
         true,
         InputCalibrationStatus::NoActiveInputDevice},
        {common::audio::MonitoringDisabledReason::NoInputDevice,
         true,
         InputCalibrationStatus::NoActiveInputDevice},
        {common::audio::MonitoringDisabledReason::MissingCalibration,
         true,
         InputCalibrationStatus::MissingCalibration},
        {common::audio::MonitoringDisabledReason::CalibrationRouteMismatch,
         true,
         InputCalibrationStatus::MissingCalibration},
        {common::audio::MonitoringDisabledReason::BackendUnavailable,
         true,
         InputCalibrationStatus::Unavailable},
        {common::audio::MonitoringDisabledReason::CalibrationStoreUnavailable,
         true,
         InputCalibrationStatus::Unavailable},
    };

    for (const Case& test_case : cases)
    {
        CHECK(
            inputCalibrationStatusFor(test_case.reason, test_case.backend_available) ==
            test_case.expected_status);
    }
}

// A calibrated matching route on a ready session projects an auditionable, calibrated projection.
TEST_CASE(
    "Input calibration projection builds an active calibrated projection", "[core][projection]")
{
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    common::audio::testing::FakeLiveInput live_input;
    common::audio::testing::ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    common::audio::testing::InMemoryAudioConfigStore store;
    REQUIRE(store.saveInputCalibration(calibrationFor(identity, 5.0)).has_value());
    common::audio::LiveInputMonitor monitor{live_input, devices, store};
    static_cast<void>(monitor.refresh(g_ready));

    const InputCalibrationProjection projection = makeInputCalibrationProjection(monitor, g_ready);

    CHECK(projection.status == InputCalibrationStatus::Calibrated);
    CHECK(projection.calibrate_enabled);
    CHECK(projection.live_input_audition_available);
    CHECK(projection.audio_device_settings_enabled);
    CHECK(projection.disabled_message.empty());
    CHECK_FALSE(projection.prompt.has_value());
}

// Opening audio-device settings still shows a matching route as calibrated, matching the status
// projection that ignores the settings-open early-out the ordered gate reports first.
TEST_CASE(
    "Input calibration projection keeps calibrated status while settings are open",
    "[core][projection]")
{
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    common::audio::testing::FakeLiveInput live_input;
    common::audio::testing::ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    common::audio::testing::InMemoryAudioConfigStore store;
    REQUIRE(store.saveInputCalibration(calibrationFor(identity, 5.0)).has_value());
    common::audio::LiveInputMonitor monitor{live_input, devices, store};
    static_cast<void>(monitor.refresh(g_ready));
    monitor.openAudioDeviceSettings();

    const InputCalibrationProjection projection = makeInputCalibrationProjection(monitor, g_ready);

    CHECK(projection.status == InputCalibrationStatus::Calibrated);
    CHECK_FALSE(projection.live_input_audition_available);
    CHECK_FALSE(projection.audio_device_settings_enabled);
    CHECK_FALSE(projection.calibrate_enabled);
}

// A visible prompt carries the matching stored gain and the disabled message for the route.
TEST_CASE(
    "Input calibration projection projects the prompt with the stored gain", "[core][projection]")
{
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    common::audio::testing::FakeLiveInput live_input;
    common::audio::testing::ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    common::audio::testing::InMemoryAudioConfigStore store;
    common::audio::LiveInputMonitor monitor{live_input, devices, store};
    static_cast<void>(monitor.refresh(g_ready));
    REQUIRE(monitor.requestPrompt(g_ready));

    const InputCalibrationProjection projection = makeInputCalibrationProjection(monitor, g_ready);

    REQUIRE(projection.prompt.has_value());
    if (projection.prompt.has_value())
    {
        CHECK(projection.prompt->message == "Live input disabled: input calibration required.");
        CHECK_THAT(
            projection.prompt->input_gain_db,
            Catch::Matchers::WithinULP(common::audio::defaultGainDb(), 0));
    }
    CHECK(projection.status == InputCalibrationStatus::MissingCalibration);
    CHECK_FALSE(projection.audio_device_settings_enabled);
}

} // namespace rock_hero::editor::core
