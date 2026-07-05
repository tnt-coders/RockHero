#include "editor_controller_impl.h"
#include "shared/editor_controller_logging.h"

#include <expected>
#include <optional>
#include <string_view>
#include <utility>

namespace rock_hero::editor::core
{

// Opens the required calibration prompt on explicit user request.
void EditorController::Impl::onInputCalibrationRequested()
{
    if (m_input_calibration.requestPrompt(inputCalibrationContext()) && m_transport.state().playing)
    {
        m_transport.pause();
    }
    updateView();
}

// Prepares the current input route for a raw calibration measurement.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationMeasurementStarted()
{
    auto measurement = m_input_calibration.prepareMeasurementStart(inputCalibrationContext());
    if (!measurement.has_value())
    {
        return std::unexpected{std::move(measurement.error())};
    }

    const InputCalibrationRouteState previous_route_state = currentInputCalibrationRouteState();
    auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
    if (!monitoring_disabled.has_value())
    {
        if (monitoring_disabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(monitoring_disabled.error())};
    }

    auto gain_reset =
        m_live_input.setInputGain(common::audio::Gain{common::audio::defaultGainDb()});
    if (!gain_reset.has_value())
    {
        restoreInputCalibrationRouteStateBestEffort(previous_route_state);
        if (gain_reset.error().code == common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(gain_reset.error())};
    }

    auto calibration_monitoring_enabled = m_live_input.setCalibrationInputMonitoringEnabled(true);
    if (!calibration_monitoring_enabled.has_value())
    {
        restoreInputCalibrationRouteStateBestEffort(previous_route_state);
        if (calibration_monitoring_enabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(calibration_monitoring_enabled.error())};
    }

    m_input_calibration.activateMeasurement(std::move(*measurement));
    updateView();
    return {};
}

// Stops an in-progress measurement without closing the calibration prompt.
void EditorController::Impl::onInputCalibrationMeasurementCancelled()
{
    if (m_input_calibration.hasActiveMeasurement())
    {
        const auto restored = restoreCalibrationMeasurementState();
        if (!restored.has_value())
        {
            reportError(restored.error().message);
        }
    }
    updateView();
}

// Applies a successful calibration gain, persists it, and enables processed monitoring.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationSucceeded(double gain_db)
{
    return commitInputCalibration(gain_db, std::nullopt);
}

// Applies a user-entered calibration gain without requiring an active automatic attempt.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationManuallySet(double gain_db)
{
    return commitInputCalibration(gain_db, std::nullopt);
}

// Closes the calibration prompt without enabling uncalibrated live monitoring.
void EditorController::Impl::onInputCalibrationDismissed()
{
    if (m_input_calibration.hasActiveMeasurement())
    {
        const auto restored = restoreCalibrationMeasurementState();
        if (!restored.has_value())
        {
            reportError(restored.error().message);
        }
    }
    m_input_calibration.closePrompt();
    updateView();
}

// Selects saved calibration for the current physical route before the live-input gate runs.
void EditorController::Impl::selectInputCalibrationForCurrentRoute()
{
    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    std::optional<common::audio::InputCalibrationState> saved_calibration;
    if (current_identity.has_value())
    {
        auto loaded_calibration = m_settings.inputCalibrationFor(*current_identity);
        if (loaded_calibration.has_value())
        {
            saved_calibration = std::move(*loaded_calibration);
        }
        else
        {
            logEditorControllerBestEffortFailure(
                "load input calibration", loaded_calibration.error().message);
        }
    }

    executeInputCalibrationEffects(m_input_calibration.syncCommittedInputDeviceIdentity(
        current_identity, std::move(saved_calibration)));
}

// Saves the active physical-route calibration after workflow and live-input side effects succeed.
void EditorController::Impl::saveActiveInputCalibration()
{
    const std::optional<common::audio::InputCalibrationState> calibration =
        m_input_calibration.activeCalibrationState();
    if (calibration.has_value())
    {
        recordSettingsResultBestEffort(
            m_settings.saveInputCalibration(*calibration), "save input calibration");
    }
}

// Executes workflow-requested side effects against root-owned ports and settings.
void EditorController::Impl::executeInputCalibrationEffects(
    const InputCalibrationWorkflow::Effects& effects)
{
    for (const InputCalibrationWorkflow::Effect effect : effects)
    {
        switch (effect)
        {
            case InputCalibrationWorkflow::Effect::DisableLiveInputMonitoring:
            {
                setLiveInputMonitoringBestEffort(false, "workflow disable live input monitoring");
                break;
            }
            case InputCalibrationWorkflow::Effect::DisableCalibrationInputMonitoring:
            {
                setCalibrationInputMonitoringBestEffort(
                    false, "workflow disable calibration monitoring");
                break;
            }
        }
    }
}

// Captures the root-owned context the calibration workflow needs to project state.
InputCalibrationWorkflow::Context EditorController::Impl::inputCalibrationContext() const
{
    return InputCalibrationWorkflow::Context{
        .project_audio_ready = m_project_audio_ready,
        .arrangement_loaded = hasLoadedArrangement(),
        .current_input_device_identity = currentInputDeviceIdentity(),
    };
}

// Applies the backend live-input gate from the current route and calibration state.
void EditorController::Impl::applyLiveInputGate()
{
    setCalibrationInputMonitoringBestEffort(false, "live-input gate calibration disable");

    if (m_input_calibration.audioDeviceSettingsOpen())
    {
        setLiveInputMonitoringBestEffort(false, "audio-device settings gate disable");
        return;
    }

    const InputCalibrationWorkflow::Context context = inputCalibrationContext();
    if (!context.project_audio_ready || !context.arrangement_loaded)
    {
        setLiveInputMonitoringBestEffort(false, "project-audio gate disable");
        return;
    }

    if (!context.current_input_device_identity.has_value())
    {
        setLiveInputMonitoringBestEffort(false, "missing-input-route gate disable");
        return;
    }

    const std::optional<common::audio::InputCalibrationState> calibration =
        m_input_calibration.activeCalibrationState();
    if (!calibration.has_value())
    {
        setLiveInputMonitoringBestEffort(false, "missing-calibration gate disable");
        return;
    }

    if (!m_input_calibration.calibrationMatches(context.current_input_device_identity))
    {
        setLiveInputMonitoringBestEffort(false, "mismatched-calibration gate disable");
        return;
    }

    auto gain_applied = m_live_input.setInputGain(calibration->calibration_gain);
    if (!gain_applied.has_value())
    {
        m_input_calibration.markBackendUnavailable();
        setLiveInputMonitoringBestEffort(false, "live-input gate gain failure disable");
        return;
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        m_input_calibration.markBackendUnavailable();
        setLiveInputMonitoringBestEffort(false, "live-input gate enable failure disable");
        return;
    }

    m_input_calibration.markBackendAvailable();
}

// Reads the current live-input route values before a multi-step calibration setup mutation.
EditorController::Impl::InputCalibrationRouteState EditorController::Impl::
    currentInputCalibrationRouteState() const
{
    return InputCalibrationRouteState{
        .input_gain = m_live_input.inputGain(),
        .live_input_monitoring_enabled = m_live_input.liveInputMonitoringEnabled(),
        .calibration_input_monitoring_enabled = m_live_input.calibrationInputMonitoringEnabled(),
    };
}

// Best-effort rollback for setup failures before an automatic attempt exists.
void EditorController::Impl::restoreInputCalibrationRouteStateBestEffort(
    const InputCalibrationRouteState& route_state)
{
    setCalibrationInputMonitoringBestEffort(
        route_state.calibration_input_monitoring_enabled,
        "restore calibration route monitoring state");
    auto gain_restored = m_live_input.setInputGain(route_state.input_gain);
    if (!gain_restored.has_value())
    {
        logEditorControllerBestEffortFailure(
            "restore calibration route input gain", gain_restored.error().message);
    }
    if (!route_state.live_input_monitoring_enabled || gain_restored.has_value())
    {
        setLiveInputMonitoringBestEffort(
            route_state.live_input_monitoring_enabled, "restore calibration route monitoring");
    }
}

bool EditorController::Impl::setLiveInputMonitoringBestEffort(
    bool enabled, std::string_view context)
{
    auto result = m_live_input.setLiveInputMonitoringEnabled(enabled);
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
        return false;
    }

    return true;
}

bool EditorController::Impl::setCalibrationInputMonitoringBestEffort(
    bool enabled, std::string_view context)
{
    auto result = m_live_input.setCalibrationInputMonitoringEnabled(enabled);
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
        return false;
    }

    return true;
}

bool EditorController::Impl::setInputGainBestEffort(
    common::audio::Gain gain, std::string_view context)
{
    auto result = m_live_input.setInputGain(gain);
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
        return false;
    }

    return true;
}

// Applies a completed calibration value to the current live route and persists it.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::commitInputCalibration(
    double gain_db, const std::optional<common::audio::InputDeviceIdentity>& expected_identity)
{
    const InputCalibrationWorkflow::Context context = inputCalibrationContext();
    const auto plan_result =
        expected_identity.has_value()
            ? m_input_calibration.prepareCommit(gain_db, expected_identity, context)
            : (m_input_calibration.hasActiveMeasurement()
                   ? m_input_calibration.prepareActiveMeasurementCommit(gain_db, context)
                   : m_input_calibration.prepareCommit(gain_db, std::nullopt, context));
    if (!plan_result.has_value())
    {
        return std::unexpected{plan_result.error()};
    }

    const InputCalibrationWorkflow::CommitPlan& plan = *plan_result;
    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        if (calibration_monitoring_disabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(calibration_monitoring_disabled.error())};
    }

    auto gain_applied = m_live_input.setInputGain(plan.calibration_gain);
    if (!gain_applied.has_value())
    {
        // The gain was not applied, so prior live-route state is intact. Only a route-unavailable
        // failure means the calibrated route is gone and must be torn down; other errors are
        // reported without disturbing existing monitoring.
        if (gain_applied.error().code == common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.preservePreviousCalibrationAfterCommitFailure(
                plan.previous_calibration_state, context.current_input_device_identity);
            saveActiveInputCalibration();
            setLiveInputMonitoringBestEffort(false, "commit calibration route-unavailable disable");
            updateView();
        }
        return std::unexpected{std::move(gain_applied.error())};
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        // The new gain is already on the live route, so roll back to the preserved calibration's
        // gain and disable monitoring regardless of the error code; otherwise the route would be
        // left armed at the rejected gain.
        const std::optional<common::audio::Gain> restore_gain =
            m_input_calibration.preservePreviousCalibrationAfterCommitFailure(
                plan.previous_calibration_state, context.current_input_device_identity);
        if (restore_gain.has_value())
        {
            setInputGainBestEffort(*restore_gain, "commit calibration gain rollback");
        }
        saveActiveInputCalibration();
        setLiveInputMonitoringBestEffort(false, "commit calibration enable-failure disable");
        updateView();
        return std::unexpected{std::move(monitoring_enabled.error())};
    }

    m_input_calibration.commitCalibration(plan.calibration_gain, plan.input_device_identity);
    saveActiveInputCalibration();
    updateView();
    return {};
}

// Restores the previous matching calibration if a manual recalibration measurement is cancelled.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    restoreCalibrationMeasurementState()
{
    using MeasurementRestore = InputCalibrationWorkflow::MeasurementRestore;

    const InputCalibrationWorkflow::MeasurementRestorePlan plan =
        m_input_calibration.prepareMeasurementRestore(inputCalibrationContext());
    if (std::holds_alternative<MeasurementRestore::NoRestore>(plan))
    {
        return {};
    }

    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        if (calibration_monitoring_disabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(calibration_monitoring_disabled.error())};
    }

    return std::visit(
        [this](const auto& restore) -> std::expected<void, common::audio::LiveInputError> {
            using Restore = std::decay_t<decltype(restore)>;
            if constexpr (std::is_same_v<Restore, MeasurementRestore::NoRestore>)
            {
                return {};
            }
            else if constexpr (std::is_same_v<Restore, MeasurementRestore::DisableLiveInput>)
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{std::move(monitoring_disabled.error())};
                }
                m_input_calibration.clearActiveMeasurement();
                return {};
            }
            else if constexpr (std::is_same_v<Restore, MeasurementRestore::ClearCalibration>)
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{std::move(monitoring_disabled.error())};
                }
                m_input_calibration.clearCalibrationAfterMeasurement();
                return {};
            }
            else if constexpr (
                std::is_same_v<Restore, MeasurementRestore::ClearCalibrationAndClosePrompt>
            )
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{std::move(monitoring_disabled.error())};
                }
                m_input_calibration.clearCalibrationAfterMeasurement();
                m_input_calibration.closePrompt();
                return {};
            }
            else
            {
                const common::audio::InputCalibrationState& previous_state =
                    restore.previous_calibration_state;

                auto gain_restored = m_live_input.setInputGain(previous_state.calibration_gain);
                if (!gain_restored.has_value())
                {
                    if (gain_restored.error().code ==
                        common::audio::LiveInputErrorCode::InputRouteUnavailable)
                    {
                        m_input_calibration.restorePreviousCalibration(previous_state, false);
                    }
                    else
                    {
                        m_input_calibration.clearCalibrationAfterMeasurement();
                    }
                    saveActiveInputCalibration();
                    setLiveInputMonitoringBestEffort(
                        false, "measurement restore gain-failure disable");
                    return std::unexpected{std::move(gain_restored.error())};
                }

                auto monitoring_restored = m_live_input.setLiveInputMonitoringEnabled(true);
                if (!monitoring_restored.has_value())
                {
                    m_input_calibration.restorePreviousCalibration(previous_state, false);
                    saveActiveInputCalibration();
                    setLiveInputMonitoringBestEffort(
                        false, "measurement restore enable-failure disable");
                    return std::unexpected{std::move(monitoring_restored.error())};
                }

                m_input_calibration.restorePreviousCalibration(previous_state, true);
                saveActiveInputCalibration();
                return {};
            }
        },
        plan);
}

// Returns the active physical input route identity, if the audio backend can provide one.
std::optional<common::audio::InputDeviceIdentity> EditorController::Impl::
    currentInputDeviceIdentity() const
{
    return m_audio_devices.currentInputDeviceIdentity();
}

} // namespace rock_hero::editor::core
