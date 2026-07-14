#include "input/input_calibration_workflow.h"

#include <string>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

[[nodiscard]] LiveInputError inputRouteUnavailable(std::string message = {})
{
    return LiveInputError{LiveInputErrorCode::InputRouteUnavailable, std::move(message)};
}

[[nodiscard]] bool contextReadyForCalibration(
    const InputCalibrationWorkflow::Context& context) noexcept
{
    return context.project_audio_ready && context.arrangement_loaded;
}

} // namespace

std::optional<InputCalibrationState> InputCalibrationWorkflow::activeCalibrationState() const
{
    return m_calibration_state;
}

void InputCalibrationWorkflow::clearCalibration()
{
    m_calibration_state.reset();
}

InputCalibrationWorkflow::Effects InputCalibrationWorkflow::syncCommittedInputDeviceIdentity(
    std::optional<InputDeviceIdentity> current_identity,
    std::optional<InputCalibrationState> saved_calibration)
{
    if (!current_identity.has_value())
    {
        if (m_audio_device_settings_open)
        {
            return {};
        }

        m_active_measurement.reset();
        m_prompt_visible = false;
        return {
            Effect::DisableCalibrationInputMonitoring,
            Effect::DisableLiveInputMonitoring,
        };
    }

    if (!m_committed_input_device_identity.has_value())
    {
        m_committed_input_device_identity = *current_identity;
        selectActiveCalibration(*current_identity, std::move(saved_calibration));
        return {};
    }

    if (samePhysicalInputRoute(*current_identity, *m_committed_input_device_identity))
    {
        m_committed_input_device_identity = *current_identity;
        if (m_calibration_state.has_value() &&
            inputCalibrationMatchesPhysicalRoute(*m_calibration_state, *current_identity))
        {
            m_calibration_state->input_device_identity = *current_identity;
        }
        else
        {
            selectActiveCalibration(*current_identity, std::move(saved_calibration));
        }
        return {};
    }

    m_committed_input_device_identity = *current_identity;
    m_active_measurement.reset();
    m_prompt_visible = false;
    m_backend_available = true;
    selectActiveCalibration(*current_identity, std::move(saved_calibration));
    return {
        Effect::DisableCalibrationInputMonitoring,
        Effect::DisableLiveInputMonitoring,
    };
}

InputCalibrationWorkflow::Effects InputCalibrationWorkflow::openAudioDeviceSettings()
{
    m_audio_device_settings_open = true;
    return {
        Effect::DisableLiveInputMonitoring,
        Effect::DisableCalibrationInputMonitoring,
    };
}

void InputCalibrationWorkflow::closeAudioDeviceSettings() noexcept
{
    m_audio_device_settings_open = false;
}

bool InputCalibrationWorkflow::audioDeviceSettingsOpen() const noexcept
{
    return m_audio_device_settings_open;
}

bool InputCalibrationWorkflow::requestPrompt(const Context& context)
{
    if (!m_audio_device_settings_open && contextReadyForCalibration(context) &&
        context.current_input_device_identity.has_value())
    {
        m_prompt_visible = true;
        return true;
    }

    return false;
}

void InputCalibrationWorkflow::closePrompt() noexcept
{
    m_prompt_visible = false;
}

bool InputCalibrationWorkflow::promptVisible() const noexcept
{
    return m_prompt_visible;
}

bool InputCalibrationWorkflow::backendAvailable() const noexcept
{
    return m_backend_available;
}

bool InputCalibrationWorkflow::calibrationMatches(
    const std::optional<InputDeviceIdentity>& current_identity) const
{
    return current_identity.has_value() && m_calibration_state.has_value() &&
           inputCalibrationMatchesPhysicalRoute(*m_calibration_state, *current_identity);
}

std::expected<InputCalibrationWorkflow::MeasurementSession, LiveInputError>
InputCalibrationWorkflow::prepareMeasurementStart(const Context& context) const
{
    if (!m_prompt_visible)
    {
        return std::unexpected{inputRouteUnavailable("Calibration prompt is not active")};
    }

    if (!contextReadyForCalibration(context))
    {
        return std::unexpected{inputRouteUnavailable("Project audio is not ready")};
    }

    if (!context.current_input_device_identity.has_value())
    {
        return std::unexpected{inputRouteUnavailable()};
    }

    std::optional<InputCalibrationState> previous_calibration_state;
    if (calibrationMatches(context.current_input_device_identity))
    {
        previous_calibration_state = m_calibration_state;
    }

    return MeasurementSession{
        .input_device_identity = *context.current_input_device_identity,
        .previous_calibration_state = std::move(previous_calibration_state),
    };
}

void InputCalibrationWorkflow::activateMeasurement(MeasurementSession measurement)
{
    m_active_measurement = std::move(measurement);
}

bool InputCalibrationWorkflow::hasActiveMeasurement() const noexcept
{
    return m_active_measurement.has_value();
}

void InputCalibrationWorkflow::clearActiveMeasurement() noexcept
{
    m_active_measurement.reset();
}

std::expected<InputCalibrationWorkflow::CommitPlan, LiveInputError> InputCalibrationWorkflow::
    prepareCommit(
        double gain_db, const std::optional<InputDeviceIdentity>& expected_identity,
        const Context& context) const
{
    if (!m_prompt_visible)
    {
        return std::unexpected{inputRouteUnavailable("Calibration prompt is not active")};
    }

    if (!contextReadyForCalibration(context))
    {
        return std::unexpected{inputRouteUnavailable("Project audio is not ready")};
    }

    if (!context.current_input_device_identity.has_value())
    {
        return std::unexpected{inputRouteUnavailable()};
    }

    if (expected_identity.has_value() &&
        !samePhysicalInputRoute(*expected_identity, *context.current_input_device_identity))
    {
        return std::unexpected{inputRouteUnavailable("Input route changed during calibration")};
    }

    return CommitPlan{
        .calibration_gain = clampGain(Gain{gain_db}),
        .input_device_identity = *context.current_input_device_identity,
        .previous_calibration_state = m_calibration_state,
    };
}

std::expected<InputCalibrationWorkflow::CommitPlan, LiveInputError> InputCalibrationWorkflow::
    prepareActiveMeasurementCommit(double gain_db, const Context& context) const
{
    if (!m_active_measurement.has_value())
    {
        return std::unexpected{inputRouteUnavailable("Calibration measurement is not active")};
    }

    return prepareCommit(gain_db, m_active_measurement->input_device_identity, context);
}

void InputCalibrationWorkflow::commitCalibration(
    Gain gain, InputDeviceIdentity input_device_identity)
{
    m_calibration_state = InputCalibrationState{
        .calibration_gain = gain,
        .input_device_identity = input_device_identity,
    };
    m_committed_input_device_identity = std::move(input_device_identity);
    m_backend_available = true;
    m_active_measurement.reset();
}

std::optional<Gain> InputCalibrationWorkflow::preservePreviousCalibrationAfterCommitFailure(
    const std::optional<InputCalibrationState>& previous_state,
    const std::optional<InputDeviceIdentity>& current_identity)
{
    std::optional<Gain> restore_gain;
    if (previous_state.has_value() && current_identity.has_value() &&
        inputCalibrationMatchesPhysicalRoute(*previous_state, *current_identity))
    {
        m_calibration_state = previous_state;
        m_calibration_state->input_device_identity = *current_identity;
        restore_gain = previous_state->calibration_gain;
    }
    else
    {
        m_calibration_state.reset();
    }

    m_active_measurement.reset();
    m_prompt_visible = false;
    m_backend_available = false;
    return restore_gain;
}

InputCalibrationWorkflow::MeasurementRestorePlan InputCalibrationWorkflow::
    prepareMeasurementRestore(const Context& context) const
{
    if (!m_active_measurement.has_value())
    {
        return MeasurementRestore::NoRestore{};
    }

    if (!contextReadyForCalibration(context))
    {
        return MeasurementRestore::DisableLiveInput{};
    }

    if (!context.current_input_device_identity.has_value() ||
        !samePhysicalInputRoute(
            *context.current_input_device_identity, m_active_measurement->input_device_identity))
    {
        return MeasurementRestore::ClearCalibrationAndClosePrompt{};
    }

    const std::optional<InputCalibrationState>& previous_state =
        m_active_measurement->previous_calibration_state;
    if (!previous_state.has_value() || !inputCalibrationMatchesPhysicalRoute(
                                           *previous_state, *context.current_input_device_identity))
    {
        return MeasurementRestore::ClearCalibration{};
    }

    InputCalibrationState restored_state = *previous_state;
    restored_state.input_device_identity = *context.current_input_device_identity;
    return MeasurementRestore::RestorePreviousCalibration{
        .previous_calibration_state = std::move(restored_state),
    };
}

void InputCalibrationWorkflow::clearCalibrationAfterMeasurement()
{
    m_calibration_state.reset();
    m_active_measurement.reset();
    m_backend_available = true;
}

void InputCalibrationWorkflow::restorePreviousCalibration(
    InputCalibrationState previous_state, bool backend_available)
{
    m_calibration_state = std::move(previous_state);
    m_active_measurement.reset();
    if (!backend_available)
    {
        m_prompt_visible = false;
    }
    m_backend_available = backend_available;
}

void InputCalibrationWorkflow::markBackendAvailable() noexcept
{
    m_backend_available = true;
}

void InputCalibrationWorkflow::markBackendUnavailable() noexcept
{
    m_active_measurement.reset();
    m_prompt_visible = false;
    m_backend_available = false;
}

void InputCalibrationWorkflow::selectActiveCalibration(
    const InputDeviceIdentity& current_identity,
    std::optional<InputCalibrationState> saved_calibration)
{
    if (saved_calibration.has_value() &&
        inputCalibrationMatchesPhysicalRoute(*saved_calibration, current_identity))
    {
        saved_calibration->input_device_identity = current_identity;
        m_calibration_state = std::move(saved_calibration);
        return;
    }

    m_calibration_state.reset();
}

} // namespace rock_hero::common::audio
