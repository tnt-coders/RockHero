#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_workflow.h>
#include <string>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Builds a physical input identity with defaults that represent one stable ASIO route.
[[nodiscard]] InputDeviceIdentity makeIdentity(
    std::string backend_name = "ASIO", std::string input_device_name = "Interface A",
    int input_channel_index = 0, std::string input_channel_name = "Input 1")
{
    return InputDeviceIdentity{
        .backend_name = std::move(backend_name),
        .input_device_name = std::move(input_device_name),
        .input_channel_index = input_channel_index,
        .input_channel_name = std::move(input_channel_name),
    };
}

// Builds controller context where arrangement audio is loaded and live input may be calibrated.
[[nodiscard]] InputCalibrationWorkflow::Context readyContext(
    std::optional<InputDeviceIdentity> identity)
{
    return InputCalibrationWorkflow::Context{
        .live_input_ready = true,
        .arrangement_loaded = true,
        .current_input_device_identity = std::move(identity),
    };
}

// Builds a saved calibration state for route-matching workflow tests.
[[nodiscard]] InputCalibrationState calibrationFor(
    const InputDeviceIdentity& identity, double gain_db = 4.0)
{
    return InputCalibrationState{
        .calibration_gain = Gain{gain_db},
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
TEST_CASE("Input calibration workflow ignores invalid saved identity", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity identity = makeIdentity();

    const InputCalibrationWorkflow::Effects effects = workflow.syncCommittedInputDeviceIdentity(
        identity,
        InputCalibrationState{
            .calibration_gain = Gain{3.0},
            .input_device_identity = {},
        });

    CHECK(effects.empty());
    CHECK_FALSE(workflow.activeCalibrationState().has_value());
}

// Startup restore should make a matching saved calibration immediately available.
TEST_CASE("Input calibration workflow preserves matching startup calibration", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity identity = makeIdentity();

    const InputCalibrationWorkflow::Effects effects =
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 5.0));

    CHECK(effects.empty());
    CHECK(workflow.calibrationMatches(identity));
    const LiveInputMonitoringStatus monitoring =
        workflow.evaluateMonitoring(readyContext(identity));
    CHECK(monitoring.state == LiveInputMonitoringState::Active);
    CHECK(monitoring.reason == MonitoringDisabledReason::None);
    CHECK(workflow.backendAvailable());
}

// Physical channels may be renamed by the OS while still representing the same input route.
TEST_CASE("Input calibration workflow accepts renamed physical channel", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity saved_identity = makeIdentity("ASIO", "Interface A", 0, "Input 1");
    const InputDeviceIdentity current_identity =
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
    "Input calibration workflow clears active state on unsaved route change", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity initial_identity = makeIdentity();
    const InputDeviceIdentity next_identity = makeIdentity("Windows Audio", "Interface B");
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
    const LiveInputMonitoringStatus monitoring =
        workflow.evaluateMonitoring(readyContext(next_identity));
    CHECK(monitoring.state == LiveInputMonitoringState::Disabled);
    CHECK(monitoring.reason == MonitoringDisabledReason::MissingCalibration);
}

// Switching routes should adopt the saved calibration supplied for the new route.
TEST_CASE("Input calibration workflow selects saved state on route change", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity initial_identity = makeIdentity();
    const InputDeviceIdentity next_identity = makeIdentity("Windows Audio", "Interface B");
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
        CHECK_THAT(calibration_state->calibration_gain.db, Catch::Matchers::WithinULP(7.0, 0));
    }
    CHECK(workflow.calibrationMatches(next_identity));
}

// Returning to a route should restore its matching saved calibration.
TEST_CASE(
    "Input calibration workflow restores saved state when returning to route", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity initial_identity = makeIdentity();
    const InputDeviceIdentity next_identity = makeIdentity("Windows Audio", "Interface B");
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
        CHECK_THAT(calibration_state->calibration_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    }
    CHECK(workflow.calibrationMatches(initial_identity));
}

// Settings dialogs may briefly report no route; that should not erase current calibration.
TEST_CASE(
    "Input calibration workflow ignores transient null route in settings", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity identity = makeIdentity();
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
    CHECK(workflow.audioDeviceSettingsOpen());
    // With settings open the ordered gate stops at the settings early-out even though the route
    // stays calibrated.
    const LiveInputMonitoringStatus monitoring =
        workflow.evaluateMonitoring(readyContext(identity));
    CHECK(monitoring.state == LiveInputMonitoringState::Disabled);
    CHECK(monitoring.reason == MonitoringDisabledReason::AudioDeviceSettingsOpen);
}

// Temporary route loss should stop prompt/measurement state but preserve saved calibration.
TEST_CASE(
    "Input calibration workflow preserves calibration through route loss", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity identity = makeIdentity();
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
        CHECK_THAT(calibration_state->calibration_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    }
    CHECK(
        workflow.evaluateMonitoring(readyContext(std::nullopt)).reason ==
        MonitoringDisabledReason::NoInputDevice);

    const InputCalibrationWorkflow::Effects restored_effects =
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 8.0));

    CHECK(restored_effects.empty());
    CHECK(workflow.calibrationMatches(identity));
    CHECK(
        workflow.evaluateMonitoring(readyContext(identity)).state ==
        LiveInputMonitoringState::Active);
}

// Backend failure should hide the prompt while preserving the last usable calibration value.
TEST_CASE("Input calibration workflow closes prompt on backend unavailable", "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity identity = makeIdentity();
    REQUIRE(
        workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0)).empty());
    REQUIRE(workflow.requestPrompt(readyContext(identity)));

    workflow.markBackendUnavailable();

    CHECK_FALSE(workflow.backendAvailable());
    CHECK_FALSE(workflow.promptVisible());
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK_THAT(calibration_state->calibration_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    }
}

// Measurement start is rejected when no calibration state has been selected for the route.
TEST_CASE("Input calibration workflow rejects stale measurement start", "[audio][workflow]")
{
    const InputCalibrationWorkflow workflow;
    const InputDeviceIdentity identity = makeIdentity();

    const auto measurement = workflow.prepareMeasurementStart(readyContext(identity));

    REQUIRE_FALSE(measurement.has_value());
    CHECK(measurement.error().code == LiveInputErrorCode::InputRouteUnavailable);
}

// Commit failure should restore the prior calibration and mark live audition unavailable.
TEST_CASE(
    "Input calibration workflow preserves previous calibration on commit failure",
    "[audio][workflow]")
{
    InputCalibrationWorkflow workflow;
    const InputDeviceIdentity identity = makeIdentity();
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

    CHECK_FALSE(workflow.backendAvailable());
    CHECK_FALSE(workflow.promptVisible());
    const auto calibration_state = workflow.activeCalibrationState();
    REQUIRE(calibration_state.has_value());
    if (calibration_state.has_value())
    {
        CHECK_THAT(calibration_state->calibration_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    }
}

// The monitoring gate reports each disabling reason in the same order as the controller's gate.
TEST_CASE("Input calibration workflow evaluateMonitoring branch matrix", "[audio][workflow]")
{
    const InputDeviceIdentity identity = makeIdentity();

    SECTION("open audio-device settings win over every other reason")
    {
        InputCalibrationWorkflow workflow;
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0))
                    .empty());
        static_cast<void>(workflow.openAudioDeviceSettings());

        const LiveInputMonitoringStatus monitoring =
            workflow.evaluateMonitoring(readyContext(identity));
        CHECK(monitoring.state == LiveInputMonitoringState::Disabled);
        CHECK(monitoring.reason == MonitoringDisabledReason::AudioDeviceSettingsOpen);
    }

    SECTION("live input not ready blocks both active monitoring and calibration")
    {
        InputCalibrationWorkflow workflow;
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0))
                    .empty());

        InputCalibrationWorkflow::Context context = readyContext(identity);
        context.live_input_ready = false;

        // Active processed monitoring reports SessionNotReady before the route and calibration
        // checks it early-outs ahead of.
        const LiveInputMonitoringStatus monitoring = workflow.evaluateMonitoring(context);
        CHECK(monitoring.state == LiveInputMonitoringState::Disabled);
        CHECK(monitoring.reason == MonitoringDisabledReason::SessionNotReady);

        // Calibration is blocked too: with no live signal to measure, the prompt refuses to open.
        CHECK(!workflow.requestPrompt(context));
    }

    SECTION("an unloaded arrangement blocks active monitoring but not calibration")
    {
        InputCalibrationWorkflow workflow;
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0))
                    .empty());

        InputCalibrationWorkflow::Context context = readyContext(identity);
        context.arrangement_loaded = false;

        // Active processed monitoring still requires an arrangement, folded into SessionNotReady.
        CHECK(
            workflow.evaluateMonitoring(context).reason ==
            MonitoringDisabledReason::SessionNotReady);

        // Calibration needs only the live input path, so the prompt opens and a raw measurement
        // can start with no arrangement loaded.
        REQUIRE(workflow.requestPrompt(context));
        CHECK(workflow.prepareMeasurementStart(context).has_value());
    }

    SECTION("a ready session without a route reports no input device")
    {
        InputCalibrationWorkflow workflow;
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0))
                    .empty());

        const LiveInputMonitoringStatus monitoring =
            workflow.evaluateMonitoring(readyContext(std::nullopt));
        CHECK(monitoring.state == LiveInputMonitoringState::Disabled);
        CHECK(monitoring.reason == MonitoringDisabledReason::NoInputDevice);
    }

    SECTION("a route with no stored calibration reports missing calibration")
    {
        InputCalibrationWorkflow workflow;
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, std::nullopt).empty());

        const LiveInputMonitoringStatus monitoring =
            workflow.evaluateMonitoring(readyContext(identity));
        CHECK(monitoring.state == LiveInputMonitoringState::Disabled);
        CHECK(monitoring.reason == MonitoringDisabledReason::MissingCalibration);
    }

    SECTION("stored calibration for a different route reports a route mismatch")
    {
        InputCalibrationWorkflow workflow;
        const InputDeviceIdentity other_identity = makeIdentity("Windows Audio", "Interface B");
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0))
                    .empty());

        // The stored calibration stays attached to the original route while the context reports a
        // different physical route, so the gate falls through to the mismatch branch.
        const LiveInputMonitoringStatus monitoring =
            workflow.evaluateMonitoring(readyContext(other_identity));
        CHECK(monitoring.state == LiveInputMonitoringState::Disabled);
        CHECK(monitoring.reason == MonitoringDisabledReason::CalibrationRouteMismatch);
    }

    SECTION("a matching calibrated route on a ready session reports active")
    {
        InputCalibrationWorkflow workflow;
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0))
                    .empty());

        const LiveInputMonitoringStatus monitoring =
            workflow.evaluateMonitoring(readyContext(identity));
        CHECK(monitoring.state == LiveInputMonitoringState::Active);
        CHECK(monitoring.reason == MonitoringDisabledReason::None);
    }

    SECTION("backend unavailability does not change the active gate result")
    {
        InputCalibrationWorkflow workflow;
        REQUIRE(workflow.syncCommittedInputDeviceIdentity(identity, calibrationFor(identity, 4.0))
                    .empty());
        workflow.markBackendUnavailable();

        // evaluateMonitoring deliberately ignores backend availability: that is a post-I/O fact the
        // downstream service layers on, not a decision the pure gate makes.
        CHECK(
            workflow.evaluateMonitoring(readyContext(identity)).state ==
            LiveInputMonitoringState::Active);
        CHECK_FALSE(workflow.backendAvailable());
    }
}

} // namespace rock_hero::common::audio
