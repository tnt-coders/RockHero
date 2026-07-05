#include "input_calibration/input_calibration_workflow.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
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

// Builds controller context where arrangement audio is loaded and live input may be calibrated.
[[nodiscard]] InputCalibrationWorkflow::Context readyContext(
    std::optional<common::audio::InputDeviceIdentity> identity)
{
    return InputCalibrationWorkflow::Context{
        .project_audio_ready = true,
        .arrangement_loaded = true,
        .current_input_device_identity = std::move(identity),
    };
}

// Builds a saved calibration state for route-matching workflow tests.
[[nodiscard]] common::audio::InputCalibrationState calibrationFor(
    const common::audio::InputDeviceIdentity& identity, double gain_db = 4.0)
{
    return common::audio::InputCalibrationState{
        .calibration_gain = common::audio::Gain{gain_db},
        .input_device_identity = identity,
    };
}

// Checks whether a transition requested a specific live-input side effect.
[[nodiscard]] bool hasEffect(
    const InputCalibrationWorkflow::Effects& effects, InputCalibrationWorkflow::Effect expected)
{
    return std::ranges::find(effects, expected) != effects.end();
}

} // namespace

// Invalid persisted calibration should not attach to a newly detected input route.
TEST_CASE("Input calibration workflow ignores invalid saved identity", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();

    const InputCalibrationWorkflow::Effects effects = workflow.syncCommittedInputDeviceIdentity(
        identity,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{3.0},
            .input_device_identity = {},
        });

    CHECK(effects.empty());
    CHECK_FALSE(workflow.activeCalibrationState().has_value());
}

// Startup restore should make a matching saved calibration immediately available.
TEST_CASE("Input calibration workflow preserves matching startup calibration", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();

    const InputCalibrationWorkflow::Effects effects =
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 5.0));

    CHECK(effects.empty());
    CHECK(workflow.calibrationMatches(identity));
    const InputCalibrationWorkflow::Snapshot snapshot = workflow.snapshot(readyContext(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Calibrated);
    CHECK(snapshot.live_input_audition_available);
}

// Physical channels may be renamed by the OS while still representing the same input route.
TEST_CASE("Input calibration workflow accepts renamed physical channel", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity saved_identity =
        makeIdentity("ASIO", "Interface A", 0, "Input 1");
    const common::audio::InputDeviceIdentity current_identity =
        makeIdentity("ASIO", "Interface A", 0, "Mic/Inst 1");

    REQUIRE(
        workflow
            .syncCommittedInputDeviceIdentity(saved_identity, calibrationFor(saved_identity, 5.0))
            .empty());

    const InputCalibrationWorkflow::Effects effects =
        workflow.syncCommittedInputDeviceIdentity(current_identity, std::nullopt);

    CHECK(effects.empty());
    CHECK(workflow.calibrationMatches(current_identity));
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK(calibration_state->input_device_identity == current_identity);
    }
}

// Switching to a different unsaved route clears prompt and measurement state together.
TEST_CASE(
    "Input calibration workflow clears active state on unsaved route change", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity initial_identity = makeIdentity();
    const common::audio::InputDeviceIdentity next_identity =
        makeIdentity("Windows Audio", "Interface B");
    REQUIRE(workflow
                .syncCommittedInputDeviceIdentity(
                    initial_identity, calibrationFor(initial_identity, 4.0))
                .empty());
    REQUIRE(workflow.requestPrompt(readyContext(initial_identity)));
    auto measurement = workflow.prepareMeasurementStart(readyContext(initial_identity));
    REQUIRE(measurement.has_value());
    workflow.activateMeasurement(std::move(*measurement));

    const InputCalibrationWorkflow::Effects effects =
        workflow.syncCommittedInputDeviceIdentity(next_identity, std::nullopt);

    CHECK(hasEffect(effects, InputCalibrationWorkflow::Effect::DisableCalibrationInputMonitoring));
    CHECK(hasEffect(effects, InputCalibrationWorkflow::Effect::DisableLiveInputMonitoring));
    CHECK_FALSE(workflow.activeCalibrationState().has_value());
    CHECK_FALSE(workflow.promptVisible());
    CHECK_FALSE(workflow.hasActiveMeasurement());
    CHECK(
        workflow.snapshot(readyContext(next_identity)).status ==
        InputCalibrationStatus::MissingCalibration);
}

// Switching routes should adopt the saved calibration supplied for the new route.
TEST_CASE("Input calibration workflow selects saved state on route change", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity initial_identity = makeIdentity();
    const common::audio::InputDeviceIdentity next_identity =
        makeIdentity("Windows Audio", "Interface B");
    REQUIRE(workflow
                .syncCommittedInputDeviceIdentity(
                    initial_identity, calibrationFor(initial_identity, 4.0))
                .empty());

    const InputCalibrationWorkflow::Effects effects = workflow.syncCommittedInputDeviceIdentity(
        next_identity, calibrationFor(next_identity, 7.0));

    CHECK(hasEffect(effects, InputCalibrationWorkflow::Effect::DisableCalibrationInputMonitoring));
    CHECK(hasEffect(effects, InputCalibrationWorkflow::Effect::DisableLiveInputMonitoring));
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK(calibration_state->calibration_gain.db == 7.0);
    }
    CHECK(workflow.calibrationMatches(next_identity));
}

// Returning to a route should restore its matching saved calibration.
TEST_CASE(
    "Input calibration workflow restores saved state when returning to route", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity initial_identity = makeIdentity();
    const common::audio::InputDeviceIdentity next_identity =
        makeIdentity("Windows Audio", "Interface B");
    REQUIRE(workflow
                .syncCommittedInputDeviceIdentity(
                    initial_identity, calibrationFor(initial_identity, 4.0))
                .empty());
    REQUIRE(
        !workflow
             .syncCommittedInputDeviceIdentity(next_identity, calibrationFor(next_identity, 7.0))
             .empty());

    const InputCalibrationWorkflow::Effects effects = workflow.syncCommittedInputDeviceIdentity(
        initial_identity, calibrationFor(initial_identity, 4.0));

    CHECK(hasEffect(effects, InputCalibrationWorkflow::Effect::DisableCalibrationInputMonitoring));
    CHECK(hasEffect(effects, InputCalibrationWorkflow::Effect::DisableLiveInputMonitoring));
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK(calibration_state->calibration_gain.db == 4.0);
    }
    CHECK(workflow.calibrationMatches(initial_identity));
}

// Settings dialogs may briefly report no route; that should not erase current calibration.
TEST_CASE("Input calibration workflow ignores transient null route in settings", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE(
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0)).empty());

    const InputCalibrationWorkflow::Effects open_effects = workflow.openAudioDeviceSettings();
    const InputCalibrationWorkflow::Effects effects =
        workflow.syncCommittedInputDeviceIdentity(std::nullopt, std::nullopt);

    CHECK(hasEffect(open_effects, InputCalibrationWorkflow::Effect::DisableLiveInputMonitoring));
    CHECK(hasEffect(
        open_effects, InputCalibrationWorkflow::Effect::DisableCalibrationInputMonitoring));
    CHECK(effects.empty());
    CHECK(workflow.calibrationMatches(identity));
    const InputCalibrationWorkflow::Snapshot snapshot = workflow.snapshot(readyContext(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Calibrated);
    CHECK_FALSE(snapshot.live_input_audition_available);
    CHECK_FALSE(snapshot.audio_device_settings_enabled);
}

// Temporary route loss should stop prompt/measurement state but preserve saved calibration.
TEST_CASE("Input calibration workflow preserves calibration through route loss", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE(
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0)).empty());
    REQUIRE(workflow.requestPrompt(readyContext(identity)));
    auto measurement = workflow.prepareMeasurementStart(readyContext(identity));
    REQUIRE(measurement.has_value());
    workflow.activateMeasurement(std::move(*measurement));

    const InputCalibrationWorkflow::Effects lost_effects =
        workflow.syncCommittedInputDeviceIdentity(std::nullopt, std::nullopt);

    CHECK(hasEffect(lost_effects, InputCalibrationWorkflow::Effect::DisableLiveInputMonitoring));
    CHECK(hasEffect(
        lost_effects, InputCalibrationWorkflow::Effect::DisableCalibrationInputMonitoring));
    CHECK_FALSE(workflow.promptVisible());
    CHECK_FALSE(workflow.hasActiveMeasurement());
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK(calibration_state->calibration_gain.db == 4.0);
    }
    CHECK(
        workflow.snapshot(readyContext(std::nullopt)).status ==
        InputCalibrationStatus::NoActiveInputDevice);

    const InputCalibrationWorkflow::Effects restored_effects =
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 8.0));

    CHECK(restored_effects.empty());
    CHECK(workflow.calibrationMatches(identity));
    const InputCalibrationWorkflow::Snapshot restored_snapshot =
        workflow.snapshot(readyContext(identity));
    CHECK(restored_snapshot.status == InputCalibrationStatus::Calibrated);
    CHECK(restored_snapshot.live_input_audition_available);
}

// Backend failure should hide the prompt while preserving the last usable calibration value.
TEST_CASE("Input calibration workflow closes prompt on backend unavailable", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE(
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0)).empty());
    REQUIRE(workflow.requestPrompt(readyContext(identity)));

    workflow.markBackendUnavailable();

    const InputCalibrationWorkflow::Snapshot snapshot = workflow.snapshot(readyContext(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Unavailable);
    CHECK_FALSE(snapshot.live_input_audition_available);
    CHECK_FALSE(snapshot.prompt.has_value());
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK(calibration_state->calibration_gain.db == 4.0);
    }
}

// Measurement start is rejected when no calibration state has been selected for the route.
TEST_CASE("Input calibration workflow rejects stale measurement start", "[core][workflow]")
{
    const InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();

    const auto measurement = workflow.prepareMeasurementStart(readyContext(identity));

    REQUIRE_FALSE(measurement.has_value());
    CHECK(measurement.error().code == common::audio::LiveInputErrorCode::InputRouteUnavailable);
}

// Commit failure should restore the prior calibration and mark live audition unavailable.
TEST_CASE(
    "Input calibration workflow preserves previous calibration on commit failure",
    "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE(
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0)).empty());
    REQUIRE(workflow.requestPrompt(readyContext(identity)));
    auto measurement = workflow.prepareMeasurementStart(readyContext(identity));
    REQUIRE(measurement.has_value());
    workflow.activateMeasurement(std::move(*measurement));

    const auto commit_plan = workflow.prepareActiveMeasurementCommit(6.0, readyContext(identity));
    REQUIRE(commit_plan.has_value());
    workflow.preservePreviousCalibrationAfterCommitFailure(
        commit_plan->previous_calibration_state, identity);

    const InputCalibrationWorkflow::Snapshot snapshot = workflow.snapshot(readyContext(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Unavailable);
    CHECK_FALSE(snapshot.prompt.has_value());
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK(calibration_state->calibration_gain.db == 4.0);
    }
}

} // namespace rock_hero::editor::core
