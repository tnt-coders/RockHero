#include "input/live_input_monitor.h"

#include <rock_hero/common/core/shared/logger.h>
#include <type_traits>
#include <utility>
#include <variant>

namespace rock_hero::common::audio
{

namespace
{

// Precondition-shaped failures the pure workflow surfaces (prompt not active, route changed,
// project not ready) become the coarse operation-level code; detail rides in the message.
[[nodiscard]] LiveInputMonitorError invalidRequestError(LiveInputError error)
{
    return LiveInputMonitorError{
        LiveInputMonitorErrorCode::InvalidRequest, std::move(error.message)
    };
}

// Any raw ILiveInput setter rejection becomes the backend-rejected code; the internal
// InputRouteUnavailable-versus-other distinction stays in the caller before this translation.
[[nodiscard]] LiveInputMonitorError backendRejectedError(LiveInputError error)
{
    return LiveInputMonitorError{
        LiveInputMonitorErrorCode::BackendRejected, std::move(error.message)
    };
}

} // namespace

LiveInputMonitor::LiveInputMonitor(
    ILiveInput& live_input, IAudioDeviceConfiguration& device_configuration,
    IAudioConfigStore& audio_config_store)
    : m_live_input(live_input)
    , m_device_configuration(device_configuration)
    , m_audio_config_store(audio_config_store)
{}

LiveInputMonitoringStatus LiveInputMonitor::refresh(LiveInputMonitoringContext context)
{
    m_context = context;
    const InputCalibrationWorkflow::Context workflow_context = workflowContext(context);
    const auto reselected = reselectCalibration(workflow_context);
    if (!reselected.has_value())
    {
        return m_status;
    }

    return applyGateInternal(workflow_context);
}

LiveInputMonitoringStatus LiveInputMonitor::applyGate(LiveInputMonitoringContext context)
{
    m_context = context;
    return applyGateInternal(workflowContext(context));
}

void LiveInputMonitor::disableMonitoring()
{
    setCalibrationInputMonitoringBestEffort(false, "live-input disable calibration monitoring");
    setLiveInputMonitoringBestEffort(false, "live-input disable monitoring");
    m_status = {
        LiveInputMonitoringState::Disabled, LiveInputMonitoringDisabledReason::SessionNotReady
    };
}

LiveInputMonitoringStatus LiveInputMonitor::status() const noexcept
{
    return m_status;
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::beginMeasurement(
    LiveInputMonitoringContext context)
{
    m_context = context;
    const InputCalibrationWorkflow::Context workflow_context = workflowContext(context);
    auto measurement = m_workflow.prepareMeasurementStart(workflow_context);
    if (!measurement.has_value())
    {
        return std::unexpected{invalidRequestError(std::move(measurement.error()))};
    }

    const RouteState previous_route_state = currentRouteState();
    auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
    if (!monitoring_disabled.has_value())
    {
        if (monitoring_disabled.error().code == LiveInputErrorCode::InputRouteUnavailable)
        {
            m_workflow.markBackendUnavailable();
        }
        return std::unexpected{backendRejectedError(std::move(monitoring_disabled.error()))};
    }

    auto gain_reset = m_live_input.setInputGain(Gain{defaultGainDb()});
    if (!gain_reset.has_value())
    {
        restoreRouteStateBestEffort(previous_route_state);
        if (gain_reset.error().code == LiveInputErrorCode::InputRouteUnavailable)
        {
            m_workflow.markBackendUnavailable();
        }
        return std::unexpected{backendRejectedError(std::move(gain_reset.error()))};
    }

    auto calibration_monitoring_enabled = m_live_input.setCalibrationInputMonitoringEnabled(true);
    if (!calibration_monitoring_enabled.has_value())
    {
        restoreRouteStateBestEffort(previous_route_state);
        if (calibration_monitoring_enabled.error().code ==
            LiveInputErrorCode::InputRouteUnavailable)
        {
            m_workflow.markBackendUnavailable();
        }
        return std::unexpected{backendRejectedError(
            std::move(calibration_monitoring_enabled.error()))};
    }

    m_workflow.activateMeasurement(std::move(*measurement));
    return {};
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::cancelMeasurement()
{
    if (!m_workflow.hasActiveMeasurement())
    {
        return {};
    }

    return restoreMeasurementState(workflowContext(m_context));
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::commitCalibration(
    double gain_db, const std::optional<InputDeviceIdentity>& expected_identity)
{
    return commitCalibrationInternal(gain_db, expected_identity);
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::setManualCalibration(double gain_db)
{
    return commitCalibrationInternal(gain_db, std::nullopt);
}

bool LiveInputMonitor::requestPrompt(LiveInputMonitoringContext context)
{
    m_context = context;
    return m_workflow.requestPrompt(workflowContext(context));
}

void LiveInputMonitor::closePrompt() noexcept
{
    m_workflow.closePrompt();
}

void LiveInputMonitor::openAudioDeviceSettings()
{
    executeInputCalibrationEffects(m_workflow.openAudioDeviceSettings());
}

LiveInputMonitoringStatus LiveInputMonitor::closeAudioDeviceSettings(
    LiveInputMonitoringContext context)
{
    m_context = context;
    const InputCalibrationWorkflow::Context workflow_context = workflowContext(context);
    const auto reselected = reselectCalibration(workflow_context);
    m_workflow.closeAudioDeviceSettings();
    if (!reselected.has_value())
    {
        return m_status;
    }

    return applyGateInternal(workflow_context);
}

std::optional<InputCalibrationState> LiveInputMonitor::activeCalibrationState() const
{
    return m_workflow.activeCalibrationState();
}

std::optional<InputDeviceIdentity> LiveInputMonitor::currentInputDeviceIdentity() const
{
    return m_device_configuration.currentInputDeviceIdentity();
}

bool LiveInputMonitor::calibrationMatchesCurrentRoute() const
{
    return m_workflow.calibrationMatches(m_device_configuration.currentInputDeviceIdentity());
}

bool LiveInputMonitor::promptVisible() const noexcept
{
    return m_workflow.promptVisible();
}

bool LiveInputMonitor::audioDeviceSettingsOpen() const noexcept
{
    return m_workflow.audioDeviceSettingsOpen();
}

bool LiveInputMonitor::backendAvailable() const noexcept
{
    return m_workflow.backendAvailable();
}

InputCalibrationWorkflow::Context LiveInputMonitor::workflowContext(
    LiveInputMonitoringContext context) const
{
    return InputCalibrationWorkflow::Context{
        .live_input_ready = context.live_input_ready,
        .arrangement_loaded = context.arrangement_loaded,
        .current_input_device_identity = m_device_configuration.currentInputDeviceIdentity(),
    };
}

LiveInputMonitoringStatus LiveInputMonitor::applyGateInternal(
    const InputCalibrationWorkflow::Context& context)
{
    setCalibrationInputMonitoringBestEffort(false, "live-input gate calibration disable");

    if (m_workflow.audioDeviceSettingsOpen())
    {
        setLiveInputMonitoringBestEffort(false, "audio-device settings gate disable");
        m_status = {
            LiveInputMonitoringState::Disabled,
            LiveInputMonitoringDisabledReason::AudioDeviceSettingsOpen
        };
        return m_status;
    }

    if (!context.live_input_ready || !context.arrangement_loaded)
    {
        setLiveInputMonitoringBestEffort(false, "live-input gate disable");
        m_status = {
            LiveInputMonitoringState::Disabled, LiveInputMonitoringDisabledReason::SessionNotReady
        };
        return m_status;
    }

    if (!context.current_input_device_identity.has_value())
    {
        setLiveInputMonitoringBestEffort(false, "missing-input-route gate disable");
        m_status = {
            LiveInputMonitoringState::Disabled, LiveInputMonitoringDisabledReason::NoInputDevice
        };
        return m_status;
    }

    const std::optional<InputCalibrationState> calibration = m_workflow.activeCalibrationState();
    if (!calibration.has_value())
    {
        setLiveInputMonitoringBestEffort(false, "missing-calibration gate disable");
        m_status = {
            LiveInputMonitoringState::Disabled,
            LiveInputMonitoringDisabledReason::MissingCalibration
        };
        return m_status;
    }

    if (!m_workflow.calibrationMatches(context.current_input_device_identity))
    {
        setLiveInputMonitoringBestEffort(false, "mismatched-calibration gate disable");
        m_status = {
            LiveInputMonitoringState::Disabled,
            LiveInputMonitoringDisabledReason::CalibrationRouteMismatch
        };
        return m_status;
    }

    auto gain_applied = m_live_input.setInputGain(calibration->calibration_gain);
    if (!gain_applied.has_value())
    {
        m_workflow.markBackendUnavailable();
        setLiveInputMonitoringBestEffort(false, "live-input gate gain failure disable");
        m_status = {
            LiveInputMonitoringState::Disabled,
            LiveInputMonitoringDisabledReason::BackendUnavailable
        };
        return m_status;
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        m_workflow.markBackendUnavailable();
        setLiveInputMonitoringBestEffort(false, "live-input gate enable failure disable");
        m_status = {
            LiveInputMonitoringState::Disabled,
            LiveInputMonitoringDisabledReason::BackendUnavailable
        };
        return m_status;
    }

    m_workflow.markBackendAvailable();
    m_status = {LiveInputMonitoringState::Active, LiveInputMonitoringDisabledReason::None};
    return m_status;
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::reselectCalibration(
    const InputCalibrationWorkflow::Context& context)
{
    const std::optional<InputDeviceIdentity>& current_identity =
        context.current_input_device_identity;
    std::optional<InputCalibrationState> saved_calibration;
    if (current_identity.has_value())
    {
        auto loaded_calibration = m_audio_config_store.inputCalibrationFor(*current_identity);
        if (!loaded_calibration.has_value())
        {
            m_status = {
                LiveInputMonitoringState::Disabled,
                LiveInputMonitoringDisabledReason::CalibrationStoreUnavailable,
            };
            RH_LOG_WARNING(
                "audio.live_input_monitor",
                "Input calibration store read failed detail={:?}",
                loaded_calibration.error().message);
            return std::unexpected{LiveInputMonitorError{
                LiveInputMonitorErrorCode::CalibrationStoreUnavailable,
                std::move(loaded_calibration.error().message),
            }};
        }

        saved_calibration = std::move(*loaded_calibration);
    }

    executeInputCalibrationEffects(m_workflow.syncCommittedInputDeviceIdentity(
        current_identity, std::move(saved_calibration)));
    return {};
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::commitCalibrationInternal(
    double gain_db, const std::optional<InputDeviceIdentity>& expected_identity)
{
    const InputCalibrationWorkflow::Context context = workflowContext(m_context);
    const auto plan_result =
        expected_identity.has_value()
            ? m_workflow.prepareCommit(gain_db, expected_identity, context)
            : (m_workflow.hasActiveMeasurement()
                   ? m_workflow.prepareActiveMeasurementCommit(gain_db, context)
                   : m_workflow.prepareCommit(gain_db, std::nullopt, context));
    if (!plan_result.has_value())
    {
        return std::unexpected{invalidRequestError(plan_result.error())};
    }

    const InputCalibrationWorkflow::CommitPlan& plan = *plan_result;
    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        if (calibration_monitoring_disabled.error().code ==
            LiveInputErrorCode::InputRouteUnavailable)
        {
            m_workflow.markBackendUnavailable();
        }
        return std::unexpected{backendRejectedError(
            std::move(calibration_monitoring_disabled.error()))};
    }

    auto gain_applied = m_live_input.setInputGain(plan.calibration_gain);
    if (!gain_applied.has_value())
    {
        // The gain was not applied, so prior live-route state is intact. Only a route-unavailable
        // failure means the calibrated route is gone and must be torn down; other errors are
        // reported without disturbing existing monitoring.
        if (gain_applied.error().code == LiveInputErrorCode::InputRouteUnavailable)
        {
            m_workflow.preservePreviousCalibrationAfterCommitFailure(
                plan.previous_calibration_state, context.current_input_device_identity);
            saveActiveInputCalibration();
            setLiveInputMonitoringBestEffort(false, "commit calibration route-unavailable disable");
        }
        return std::unexpected{backendRejectedError(std::move(gain_applied.error()))};
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        // The new gain is already on the live route, so roll back to the preserved calibration's
        // gain and disable monitoring regardless of the error code; otherwise the route would be
        // left armed at the rejected gain.
        const std::optional<Gain> restore_gain =
            m_workflow.preservePreviousCalibrationAfterCommitFailure(
                plan.previous_calibration_state, context.current_input_device_identity);
        if (restore_gain.has_value())
        {
            setInputGainBestEffort(*restore_gain, "commit calibration gain rollback");
        }
        saveActiveInputCalibration();
        setLiveInputMonitoringBestEffort(false, "commit calibration enable-failure disable");
        return std::unexpected{backendRejectedError(std::move(monitoring_enabled.error()))};
    }

    m_workflow.commitCalibration(plan.calibration_gain, plan.input_device_identity);
    saveActiveInputCalibration();
    return {};
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::restoreMeasurementState(
    const InputCalibrationWorkflow::Context& context)
{
    using MeasurementRestore = InputCalibrationWorkflow::MeasurementRestore;

    const InputCalibrationWorkflow::MeasurementRestorePlan plan =
        m_workflow.prepareMeasurementRestore(context);
    if (std::holds_alternative<MeasurementRestore::NoRestore>(plan))
    {
        return {};
    }

    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        if (calibration_monitoring_disabled.error().code ==
            LiveInputErrorCode::InputRouteUnavailable)
        {
            m_workflow.markBackendUnavailable();
        }
        return std::unexpected{backendRejectedError(
            std::move(calibration_monitoring_disabled.error()))};
    }

    return std::visit(
        [this](const auto& restore) -> std::expected<void, LiveInputMonitorError> {
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
                    return std::unexpected{backendRejectedError(
                        std::move(monitoring_disabled.error()))};
                }
                m_workflow.clearActiveMeasurement();
                return {};
            }
            else if constexpr (std::is_same_v<Restore, MeasurementRestore::ClearCalibration>)
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{backendRejectedError(
                        std::move(monitoring_disabled.error()))};
                }
                m_workflow.clearCalibrationAfterMeasurement();
                return {};
            }
            else if constexpr (
                std::is_same_v<Restore, MeasurementRestore::ClearCalibrationAndClosePrompt>
            )
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{backendRejectedError(
                        std::move(monitoring_disabled.error()))};
                }
                m_workflow.clearCalibrationAfterMeasurement();
                m_workflow.closePrompt();
                return {};
            }
            else
            {
                const InputCalibrationState& previous_state = restore.previous_calibration_state;

                auto gain_restored = m_live_input.setInputGain(previous_state.calibration_gain);
                if (!gain_restored.has_value())
                {
                    if (gain_restored.error().code == LiveInputErrorCode::InputRouteUnavailable)
                    {
                        m_workflow.restorePreviousCalibration(previous_state, false);
                    }
                    else
                    {
                        m_workflow.clearCalibrationAfterMeasurement();
                    }
                    saveActiveInputCalibration();
                    setLiveInputMonitoringBestEffort(
                        false, "measurement restore gain-failure disable");
                    return std::unexpected{backendRejectedError(std::move(gain_restored.error()))};
                }

                auto monitoring_restored = m_live_input.setLiveInputMonitoringEnabled(true);
                if (!monitoring_restored.has_value())
                {
                    m_workflow.restorePreviousCalibration(previous_state, false);
                    saveActiveInputCalibration();
                    setLiveInputMonitoringBestEffort(
                        false, "measurement restore enable-failure disable");
                    return std::unexpected{backendRejectedError(
                        std::move(monitoring_restored.error()))};
                }

                m_workflow.restorePreviousCalibration(previous_state, true);
                saveActiveInputCalibration();
                return {};
            }
        },
        plan);
}

void LiveInputMonitor::saveActiveInputCalibration()
{
    const std::optional<InputCalibrationState> calibration = m_workflow.activeCalibrationState();
    if (calibration.has_value())
    {
        const auto saved = m_audio_config_store.saveInputCalibration(*calibration);
        if (!saved.has_value())
        {
            RH_LOG_WARNING(
                "audio.live_input_monitor",
                "Input calibration store write failed detail={:?}",
                saved.error().message);
        }
    }
}

LiveInputMonitor::RouteState LiveInputMonitor::currentRouteState() const
{
    return RouteState{
        .input_gain = m_live_input.inputGain(),
        .live_input_monitoring_enabled = m_live_input.liveInputMonitoringEnabled(),
        .calibration_input_monitoring_enabled = m_live_input.calibrationInputMonitoringEnabled(),
    };
}

void LiveInputMonitor::restoreRouteStateBestEffort(const RouteState& route_state)
{
    setCalibrationInputMonitoringBestEffort(
        route_state.calibration_input_monitoring_enabled,
        "restore calibration route monitoring state");
    auto gain_restored = m_live_input.setInputGain(route_state.input_gain);
    if (!gain_restored.has_value())
    {
        RH_LOG_WARNING(
            "audio.live_input_monitor",
            "Best-effort cleanup or persistence failed context={:?} detail={:?}",
            "restore calibration route input gain",
            gain_restored.error().message);
    }
    if (!route_state.live_input_monitoring_enabled || gain_restored.has_value())
    {
        setLiveInputMonitoringBestEffort(
            route_state.live_input_monitoring_enabled, "restore calibration route monitoring");
    }
}

void LiveInputMonitor::executeInputCalibrationEffects(
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

bool LiveInputMonitor::setLiveInputMonitoringBestEffort(bool enabled, std::string_view reason)
{
    auto result = m_live_input.setLiveInputMonitoringEnabled(enabled);
    if (!result.has_value())
    {
        RH_LOG_WARNING(
            "audio.live_input_monitor",
            "Best-effort cleanup or persistence failed context={:?} detail={:?}",
            reason,
            result.error().message);
        return false;
    }

    return true;
}

bool LiveInputMonitor::setCalibrationInputMonitoringBestEffort(
    bool enabled, std::string_view reason)
{
    auto result = m_live_input.setCalibrationInputMonitoringEnabled(enabled);
    if (!result.has_value())
    {
        RH_LOG_WARNING(
            "audio.live_input_monitor",
            "Best-effort cleanup or persistence failed context={:?} detail={:?}",
            reason,
            result.error().message);
        return false;
    }

    return true;
}

bool LiveInputMonitor::setInputGainBestEffort(Gain gain, std::string_view reason)
{
    auto result = m_live_input.setInputGain(gain);
    if (!result.has_value())
    {
        RH_LOG_WARNING(
            "audio.live_input_monitor",
            "Best-effort cleanup or persistence failed context={:?} detail={:?}",
            reason,
            result.error().message);
        return false;
    }

    return true;
}

} // namespace rock_hero::common::audio
