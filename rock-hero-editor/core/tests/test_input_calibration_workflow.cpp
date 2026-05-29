#include "input_calibration_workflow.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] common::audio::InputDeviceIdentity makeIdentity(
    std::string backend_name = "ASIO", std::string input_device_name = "Interface A",
    int input_channel_index = 0)
{
    return common::audio::InputDeviceIdentity{
        .backend_name = std::move(backend_name),
        .input_device_name = std::move(input_device_name),
        .input_channel_index = input_channel_index,
        .input_channel_name = "Input 1",
    };
}

[[nodiscard]] InputCalibrationFacts readyFacts(
    std::optional<common::audio::InputDeviceIdentity> identity)
{
    return InputCalibrationFacts{
        .project_audio_ready = true,
        .arrangement_loaded = true,
        .current_input_device_identity = std::move(identity),
    };
}

[[nodiscard]] common::audio::InputCalibrationState calibrationFor(
    const common::audio::InputDeviceIdentity& identity, double gain_db = 4.0)
{
    return common::audio::InputCalibrationState{
        .calibration_gain = common::audio::Gain{gain_db},
        .input_device_identity = identity,
    };
}

[[nodiscard]] bool hasEffect(
    const InputCalibrationEffects& effects, InputCalibrationEffect expected)
{
    return std::ranges::find(effects, expected) != effects.end();
}

} // namespace

TEST_CASE("Input calibration workflow drops invalid persisted identity", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;

    const bool should_persist_cleanup = workflow.load(
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{3.0},
            .input_device_identity = {},
        });

    CHECK(should_persist_cleanup);
    CHECK_FALSE(workflow.calibrationState().has_value());
}

TEST_CASE("Input calibration workflow preserves matching startup calibration", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();

    CHECK_FALSE(workflow.load(calibrationFor(identity, 5.0)));
    const InputCalibrationEffects effects = workflow.syncCommittedInputDeviceIdentity(identity);

    CHECK(effects.empty());
    CHECK(workflow.calibrationMatches(identity));
    const InputCalibrationSnapshot snapshot = workflow.snapshot(readyFacts(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Calibrated);
    CHECK(snapshot.live_input_audition_available);
}

TEST_CASE("Input calibration workflow clears state on committed route change", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity initial_identity = makeIdentity();
    const common::audio::InputDeviceIdentity next_identity =
        makeIdentity("Windows Audio", "Interface B");
    REQUIRE_FALSE(workflow.load(calibrationFor(initial_identity, 4.0)));
    REQUIRE(workflow.syncCommittedInputDeviceIdentity(initial_identity).empty());
    REQUIRE(workflow.requestPrompt(readyFacts(initial_identity)));
    auto measurement = workflow.prepareMeasurementStart(readyFacts(initial_identity));
    REQUIRE(measurement.has_value());
    workflow.activateMeasurement(std::move(*measurement));

    const InputCalibrationEffects effects =
        workflow.syncCommittedInputDeviceIdentity(next_identity);

    CHECK(hasEffect(effects, InputCalibrationEffect::PersistCalibration));
    CHECK(hasEffect(effects, InputCalibrationEffect::DisableCalibrationInputMonitoring));
    CHECK(hasEffect(effects, InputCalibrationEffect::DisableLiveInputMonitoring));
    CHECK_FALSE(workflow.calibrationState().has_value());
    CHECK_FALSE(workflow.promptVisible());
    CHECK_FALSE(workflow.hasActiveMeasurement());
    CHECK(
        workflow.snapshot(readyFacts(next_identity)).status ==
        InputCalibrationStatus::MissingCalibration);
}

TEST_CASE("Input calibration workflow ignores transient null route in settings", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE_FALSE(workflow.load(calibrationFor(identity, 4.0)));
    REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity).empty());

    const InputCalibrationEffects open_effects = workflow.openAudioDeviceSettings();
    const InputCalibrationEffects effects = workflow.syncCommittedInputDeviceIdentity(std::nullopt);

    CHECK(hasEffect(open_effects, InputCalibrationEffect::DisableLiveInputMonitoring));
    CHECK(hasEffect(open_effects, InputCalibrationEffect::DisableCalibrationInputMonitoring));
    CHECK(effects.empty());
    CHECK(workflow.calibrationMatches(identity));
    const InputCalibrationSnapshot snapshot = workflow.snapshot(readyFacts(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Calibrated);
    CHECK_FALSE(snapshot.live_input_audition_available);
    CHECK_FALSE(snapshot.audio_device_settings_enabled);
}

TEST_CASE("Input calibration workflow closes prompt on backend unavailable", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE_FALSE(workflow.load(calibrationFor(identity, 4.0)));
    REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity).empty());
    REQUIRE(workflow.requestPrompt(readyFacts(identity)));

    workflow.markBackendUnavailable();

    const InputCalibrationSnapshot snapshot = workflow.snapshot(readyFacts(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Unavailable);
    CHECK_FALSE(snapshot.live_input_audition_available);
    CHECK_FALSE(snapshot.prompt.has_value());
    REQUIRE(workflow.calibrationState().has_value());
    CHECK(workflow.calibrationState()->calibration_gain.db == 4.0);
}

TEST_CASE("Input calibration workflow rejects stale measurement start", "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();

    const auto measurement = workflow.prepareMeasurementStart(readyFacts(identity));

    REQUIRE_FALSE(measurement.has_value());
    CHECK(measurement.error().code == common::audio::LiveInputErrorCode::InputRouteUnavailable);
}

TEST_CASE(
    "Input calibration workflow preserves previous calibration on commit failure",
    "[core][workflow]")
{
    InputCalibrationWorkflow workflow;
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    REQUIRE_FALSE(workflow.load(calibrationFor(identity, 4.0)));
    REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity).empty());
    REQUIRE(workflow.requestPrompt(readyFacts(identity)));
    auto measurement = workflow.prepareMeasurementStart(readyFacts(identity));
    REQUIRE(measurement.has_value());
    workflow.activateMeasurement(std::move(*measurement));

    const auto commit_plan = workflow.prepareActiveMeasurementCommit(6.0, readyFacts(identity));
    REQUIRE(commit_plan.has_value());
    workflow.preservePreviousCalibrationAfterCommitFailure(
        commit_plan->previous_calibration_state, identity);

    const InputCalibrationSnapshot snapshot = workflow.snapshot(readyFacts(identity));
    CHECK(snapshot.status == InputCalibrationStatus::Unavailable);
    CHECK_FALSE(snapshot.prompt.has_value());
    REQUIRE(workflow.calibrationState().has_value());
    CHECK(workflow.calibrationState()->calibration_gain.db == 4.0);
}

} // namespace rock_hero::editor::core
