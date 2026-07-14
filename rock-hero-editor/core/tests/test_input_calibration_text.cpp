#include "input_calibration/input_calibration_text.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
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

// Builds controller context where arrangement audio is loaded and live input may be calibrated.
[[nodiscard]] common::audio::InputCalibrationWorkflow::Context readyContext(
    std::optional<common::audio::InputDeviceIdentity> identity)
{
    return common::audio::InputCalibrationWorkflow::Context{
        .project_audio_ready = true,
        .arrangement_loaded = true,
        .current_input_device_identity = std::move(identity),
    };
}

} // namespace

// Each monitoring reason projects to a fixed signal-chain status and disabled message. This pins
// the editor English and the enum mapping the projection module owns after the workflow moved to
// common/audio.
TEST_CASE(
    "Input calibration text maps each monitoring reason to status and message", "[core][text]")
{
    struct Case
    {
        common::audio::MonitoringDisabledReason reason;
        bool backend_available;
        InputCalibrationStatus expected_status;
        std::string expected_message;
    };

    const std::string no_device = "Live input disabled: no audio input device selected.";
    const std::string missing = "Live input disabled: input calibration required.";
    const std::string unavailable = "Live input disabled: live input backend unavailable.";

    const Case cases[] = {
        {common::audio::MonitoringDisabledReason::None,
         true,
         InputCalibrationStatus::Calibrated,
         {}},
        {common::audio::MonitoringDisabledReason::None,
         false,
         InputCalibrationStatus::Unavailable,
         unavailable},
        {common::audio::MonitoringDisabledReason::AudioDeviceSettingsOpen,
         true,
         InputCalibrationStatus::NoActiveInputDevice,
         no_device},
        {common::audio::MonitoringDisabledReason::SessionNotReady,
         true,
         InputCalibrationStatus::NoActiveInputDevice,
         no_device},
        {common::audio::MonitoringDisabledReason::NoInputDevice,
         true,
         InputCalibrationStatus::NoActiveInputDevice,
         no_device},
        {common::audio::MonitoringDisabledReason::MissingCalibration,
         true,
         InputCalibrationStatus::MissingCalibration,
         missing},
        {common::audio::MonitoringDisabledReason::CalibrationRouteMismatch,
         true,
         InputCalibrationStatus::MissingCalibration,
         missing},
        {common::audio::MonitoringDisabledReason::BackendUnavailable,
         true,
         InputCalibrationStatus::Unavailable,
         unavailable},
        {common::audio::MonitoringDisabledReason::CalibrationStoreUnavailable,
         true,
         InputCalibrationStatus::Unavailable,
         unavailable},
    };

    for (const Case& test_case : cases)
    {
        const InputCalibrationStatus status =
            inputCalibrationStatusFor(test_case.reason, test_case.backend_available);
        CHECK(status == test_case.expected_status);
        CHECK(inputCalibrationDisabledMessageFor(status) == test_case.expected_message);
    }
}

// A calibrated matching route on a ready session projects an auditionable, calibrated slice.
TEST_CASE("Input calibration text builds an active calibrated slice", "[core][text]")
{
    common::audio::InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE(
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 5.0)).empty());

    const InputCalibrationViewSlice slice =
        makeInputCalibrationViewState(workflow, readyContext(identity));

    CHECK(slice.status == InputCalibrationStatus::Calibrated);
    CHECK(slice.calibrate_enabled);
    CHECK(slice.live_input_audition_available);
    CHECK(slice.audio_device_settings_enabled);
    CHECK(slice.disabled_message.empty());
    CHECK_FALSE(slice.prompt.has_value());
}

// Opening audio-device settings still shows a matching route as calibrated, matching the
// pre-Phase-2 workflow status() that ignored the settings-open early-out.
TEST_CASE("Input calibration text keeps calibrated status while settings are open", "[core][text]")
{
    common::audio::InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE(
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 5.0)).empty());
    static_cast<void>(workflow.openAudioDeviceSettings());

    const InputCalibrationViewSlice slice =
        makeInputCalibrationViewState(workflow, readyContext(identity));

    CHECK(slice.status == InputCalibrationStatus::Calibrated);
    CHECK_FALSE(slice.live_input_audition_available);
    CHECK_FALSE(slice.audio_device_settings_enabled);
    CHECK_FALSE(slice.calibrate_enabled);
}

// A visible prompt carries the matching stored gain and the disabled message for the route.
TEST_CASE("Input calibration text projects the prompt with the stored gain", "[core][text]")
{
    common::audio::InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, std::nullopt).empty());
    REQUIRE(workflow.requestPrompt(readyContext(identity)));

    const InputCalibrationViewSlice slice =
        makeInputCalibrationViewState(workflow, readyContext(identity));

    REQUIRE(slice.prompt.has_value());
    if (slice.prompt.has_value())
    {
        CHECK(slice.prompt->message == "Live input disabled: input calibration required.");
        CHECK_THAT(
            slice.prompt->input_gain_db,
            Catch::Matchers::WithinULP(common::audio::defaultGainDb(), 0));
    }
    CHECK(slice.status == InputCalibrationStatus::MissingCalibration);
    CHECK_FALSE(slice.audio_device_settings_enabled);
}

} // namespace rock_hero::editor::core
