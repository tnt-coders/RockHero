#include "input_calibration_workflow.h"

#include <utility>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] common::audio::LiveInputError inputRouteUnavailable(std::string message = {})
{
    return common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable, std::move(message)
    };
}

[[nodiscard]] bool contextReadyForCalibration(
    const InputCalibrationWorkflow::Context& context) noexcept
{
    return context.project_audio_ready && context.arrangement_loaded;
}

} // namespace

std::optional<common::audio::InputCalibrationState> InputCalibrationWorkflow::
    activeCalibrationState() const
{
    return m_calibration_state;
}

void InputCalibrationWorkflow::clearCalibration()
{
    m_calibration_state.reset();
}

InputCalibrationWorkflow::Effects InputCalibrationWorkflow::syncCommittedInputDeviceIdentity(
    std::optional<common::audio::InputDeviceIdentity> current_identity,
    std::optional<common::audio::InputCalibrationState> saved_calibration)
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

    if (common::audio::samePhysicalInputRoute(
            *current_identity, *m_committed_input_device_identity))
    {
        m_committed_input_device_identity = *current_identity;
        if (m_calibration_state.has_value() && common::audio::inputCalibrationMatchesPhysicalRoute(
                                                   *m_calibration_state, *current_identity))
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

InputCalibrationWorkflow::Snapshot InputCalibrationWorkflow::snapshot(const Context& context) const
{
    const bool audition_available = contextReadyForCalibration(context) &&
                                    calibrationMatches(context.current_input_device_identity) &&
                                    m_backend_available && !m_prompt_visible &&
                                    !m_audio_device_settings_open;
    Snapshot result{
        .status = status(context.current_input_device_identity),
        .calibrate_enabled = contextReadyForCalibration(context) && !m_audio_device_settings_open &&
                             context.current_input_device_identity.has_value(),
        .live_input_audition_available = audition_available,
        .audio_device_settings_enabled = !m_prompt_visible && !m_audio_device_settings_open,
        .disabled_message = audition_available
                                ? std::string{}
                                : disabledMessage(context.current_input_device_identity),
        .prompt = std::nullopt,
    };

    if (m_prompt_visible)
    {
        result.prompt = InputCalibrationPrompt{
            .message = disabledMessage(context.current_input_device_identity),
            .input_gain_db = promptGainDb(context.current_input_device_identity),
        };
    }

    return result;
}

bool InputCalibrationWorkflow::calibrationMatches(
    const std::optional<common::audio::InputDeviceIdentity>& current_identity) const
{
    return current_identity.has_value() && m_calibration_state.has_value() &&
           common::audio::inputCalibrationMatchesPhysicalRoute(
               *m_calibration_state, *current_identity);
}

std::expected<InputCalibrationWorkflow::MeasurementSession, common::audio::LiveInputError>
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

    std::optional<common::audio::InputCalibrationState> previous_calibration_state;
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

std::expected<InputCalibrationWorkflow::CommitPlan, common::audio::LiveInputError>
InputCalibrationWorkflow::prepareCommit(
    double gain_db, const std::optional<common::audio::InputDeviceIdentity>& expected_identity,
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
        !common::audio::samePhysicalInputRoute(
            *expected_identity, *context.current_input_device_identity))
    {
        return std::unexpected{inputRouteUnavailable("Input route changed during calibration")};
    }

    return CommitPlan{
        .calibration_gain = common::audio::clampGain(common::audio::Gain{gain_db}),
        .input_device_identity = *context.current_input_device_identity,
        .previous_calibration_state = m_calibration_state,
    };
}

std::expected<InputCalibrationWorkflow::CommitPlan, common::audio::LiveInputError>
InputCalibrationWorkflow::prepareActiveMeasurementCommit(
    double gain_db, const Context& context) const
{
    if (!m_active_measurement.has_value())
    {
        return std::unexpected{inputRouteUnavailable("Calibration measurement is not active")};
    }

    return prepareCommit(gain_db, m_active_measurement->input_device_identity, context);
}

void InputCalibrationWorkflow::commitCalibration(
    common::audio::Gain gain, common::audio::InputDeviceIdentity input_device_identity)
{
    m_calibration_state = common::audio::InputCalibrationState{
        .calibration_gain = gain,
        .input_device_identity = input_device_identity,
    };
    m_committed_input_device_identity = std::move(input_device_identity);
    m_backend_available = true;
    m_active_measurement.reset();
}

std::optional<common::audio::Gain> InputCalibrationWorkflow::
    preservePreviousCalibrationAfterCommitFailure(
        const std::optional<common::audio::InputCalibrationState>& previous_state,
        const std::optional<common::audio::InputDeviceIdentity>& current_identity)
{
    std::optional<common::audio::Gain> restore_gain;
    if (previous_state.has_value() && current_identity.has_value() &&
        common::audio::inputCalibrationMatchesPhysicalRoute(*previous_state, *current_identity))
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
        !common::audio::samePhysicalInputRoute(
            *context.current_input_device_identity, m_active_measurement->input_device_identity))
    {
        return MeasurementRestore::ClearCalibrationAndClosePrompt{};
    }

    const std::optional<common::audio::InputCalibrationState>& previous_state =
        m_active_measurement->previous_calibration_state;
    if (!previous_state.has_value() || !common::audio::inputCalibrationMatchesPhysicalRoute(
                                           *previous_state, *context.current_input_device_identity))
    {
        return MeasurementRestore::ClearCalibration{};
    }

    common::audio::InputCalibrationState restored_state = *previous_state;
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
    common::audio::InputCalibrationState previous_state, bool backend_available)
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

InputCalibrationStatus InputCalibrationWorkflow::status(
    const std::optional<common::audio::InputDeviceIdentity>& current_identity) const
{
    if (!current_identity.has_value())
    {
        return InputCalibrationStatus::NoActiveInputDevice;
    }

    if (!calibrationMatches(current_identity))
    {
        return InputCalibrationStatus::MissingCalibration;
    }

    return m_backend_available ? InputCalibrationStatus::Calibrated
                               : InputCalibrationStatus::Unavailable;
}

std::string InputCalibrationWorkflow::disabledMessage(
    const std::optional<common::audio::InputDeviceIdentity>& current_identity) const
{
    switch (status(current_identity))
    {
        case InputCalibrationStatus::NoActiveInputDevice:
        {
            return "Live input disabled: no audio input device selected.";
        }
        case InputCalibrationStatus::MissingCalibration:
        {
            return "Live input disabled: input calibration required.";
        }
        case InputCalibrationStatus::Calibrated:
        {
            return {};
        }
        case InputCalibrationStatus::Unavailable:
        {
            return "Live input disabled: live input backend unavailable.";
        }
    }

    return "Live input disabled.";
}

double InputCalibrationWorkflow::promptGainDb(
    const std::optional<common::audio::InputDeviceIdentity>& current_identity) const
{
    if (!current_identity.has_value() || !m_calibration_state.has_value())
    {
        return common::audio::defaultGainDb();
    }

    if (!common::audio::inputCalibrationMatchesPhysicalRoute(
            *m_calibration_state, *current_identity))
    {
        return common::audio::defaultGainDb();
    }

    return m_calibration_state->calibration_gain.db;
}

void InputCalibrationWorkflow::selectActiveCalibration(
    const common::audio::InputDeviceIdentity& current_identity,
    std::optional<common::audio::InputCalibrationState> saved_calibration)
{
    if (saved_calibration.has_value() &&
        common::audio::inputCalibrationMatchesPhysicalRoute(*saved_calibration, current_identity))
    {
        saved_calibration->input_device_identity = current_identity;
        m_calibration_state = std::move(saved_calibration);
        return;
    }

    m_calibration_state.reset();
}

} // namespace rock_hero::editor::core
