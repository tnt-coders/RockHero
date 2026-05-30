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

[[nodiscard]] bool factsReadyForCalibration(const InputCalibrationFacts& facts) noexcept
{
    return facts.project_audio_ready && facts.arrangement_loaded;
}

} // namespace

bool InputCalibrationWorkflow::load(
    std::optional<common::audio::InputCalibrationState> calibration_state)
{
    m_calibration_state = std::move(calibration_state);
    if (m_calibration_state.has_value() &&
        !common::audio::isValidInputDeviceIdentity(m_calibration_state->input_device_identity))
    {
        m_calibration_state.reset();
        return true;
    }

    return false;
}

std::optional<common::audio::InputCalibrationState> InputCalibrationWorkflow::calibrationState()
    const
{
    return m_calibration_state;
}

void InputCalibrationWorkflow::clearCalibration()
{
    m_calibration_state.reset();
}

InputCalibrationEffects InputCalibrationWorkflow::syncCommittedInputDeviceIdentity(
    std::optional<common::audio::InputDeviceIdentity> current_identity)
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
            InputCalibrationEffect::DisableCalibrationInputMonitoring,
            InputCalibrationEffect::DisableLiveInputMonitoring,
        };
    }

    if (!m_committed_input_device_identity.has_value())
    {
        m_committed_input_device_identity = *current_identity;
        if (m_calibration_state.has_value() && !calibrationMatches(current_identity))
        {
            m_calibration_state.reset();
            return {InputCalibrationEffect::PersistCalibration};
        }

        return {};
    }

    if (current_identity == m_committed_input_device_identity)
    {
        return {};
    }

    m_committed_input_device_identity = std::move(current_identity);
    m_active_measurement.reset();
    m_prompt_visible = false;
    m_backend_available = true;
    m_calibration_state.reset();
    return {
        InputCalibrationEffect::PersistCalibration,
        InputCalibrationEffect::DisableCalibrationInputMonitoring,
        InputCalibrationEffect::DisableLiveInputMonitoring,
    };
}

InputCalibrationEffects InputCalibrationWorkflow::openAudioDeviceSettings()
{
    m_audio_device_settings_open = true;
    return {
        InputCalibrationEffect::DisableLiveInputMonitoring,
        InputCalibrationEffect::DisableCalibrationInputMonitoring,
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

bool InputCalibrationWorkflow::requestPrompt(const InputCalibrationFacts& facts)
{
    if (!m_audio_device_settings_open && factsReadyForCalibration(facts) &&
        facts.current_input_device_identity.has_value())
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

InputCalibrationSnapshot InputCalibrationWorkflow::snapshot(
    const InputCalibrationFacts& facts) const
{
    const bool audition_available = factsReadyForCalibration(facts) &&
                                    calibrationMatches(facts.current_input_device_identity) &&
                                    m_backend_available && !m_prompt_visible &&
                                    !m_audio_device_settings_open;
    InputCalibrationSnapshot result{
        .status = status(facts.current_input_device_identity),
        .calibrate_enabled = factsReadyForCalibration(facts) && !m_audio_device_settings_open &&
                             facts.current_input_device_identity.has_value(),
        .live_input_audition_available = audition_available,
        .audio_device_settings_enabled = !m_prompt_visible && !m_audio_device_settings_open,
        .disabled_message = audition_available
                                ? std::string{}
                                : disabledMessage(facts.current_input_device_identity),
        .prompt = std::nullopt,
    };

    if (m_prompt_visible)
    {
        result.prompt = InputCalibrationPrompt{
            .message = disabledMessage(facts.current_input_device_identity),
            .input_gain_db = promptGainDb(facts.current_input_device_identity),
        };
    }

    return result;
}

bool InputCalibrationWorkflow::calibrationMatches(
    const std::optional<common::audio::InputDeviceIdentity>& current_identity) const
{
    return current_identity.has_value() && m_calibration_state.has_value() &&
           common::audio::inputCalibrationMatchesIdentity(*m_calibration_state, *current_identity);
}

std::expected<InputCalibrationMeasurement, common::audio::LiveInputError> InputCalibrationWorkflow::
    prepareMeasurementStart(const InputCalibrationFacts& facts) const
{
    if (!m_prompt_visible)
    {
        return std::unexpected{inputRouteUnavailable("Calibration prompt is not active")};
    }

    if (!factsReadyForCalibration(facts))
    {
        return std::unexpected{inputRouteUnavailable("Project audio is not ready")};
    }

    if (!facts.current_input_device_identity.has_value())
    {
        return std::unexpected{inputRouteUnavailable()};
    }

    std::optional<common::audio::InputCalibrationState> previous_calibration_state;
    if (calibrationMatches(facts.current_input_device_identity))
    {
        previous_calibration_state = m_calibration_state;
    }

    return InputCalibrationMeasurement{
        .input_device_identity = *facts.current_input_device_identity,
        .previous_calibration_state = std::move(previous_calibration_state),
    };
}

void InputCalibrationWorkflow::activateMeasurement(InputCalibrationMeasurement measurement)
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

std::expected<InputCalibrationCommitPlan, common::audio::LiveInputError> InputCalibrationWorkflow::
    prepareCommit(
        double gain_db, const std::optional<common::audio::InputDeviceIdentity>& expected_identity,
        const InputCalibrationFacts& facts) const
{
    if (!m_prompt_visible)
    {
        return std::unexpected{inputRouteUnavailable("Calibration prompt is not active")};
    }

    if (!factsReadyForCalibration(facts))
    {
        return std::unexpected{inputRouteUnavailable("Project audio is not ready")};
    }

    if (!facts.current_input_device_identity.has_value())
    {
        return std::unexpected{inputRouteUnavailable()};
    }

    if (expected_identity.has_value() && *expected_identity != *facts.current_input_device_identity)
    {
        return std::unexpected{inputRouteUnavailable("Input route changed during calibration")};
    }

    return InputCalibrationCommitPlan{
        .calibration_gain = common::audio::clampGain(common::audio::Gain{gain_db}),
        .input_device_identity = *facts.current_input_device_identity,
        .previous_calibration_state = m_calibration_state,
    };
}

std::expected<InputCalibrationCommitPlan, common::audio::LiveInputError> InputCalibrationWorkflow::
    prepareActiveMeasurementCommit(double gain_db, const InputCalibrationFacts& facts) const
{
    if (!m_active_measurement.has_value())
    {
        return std::unexpected{inputRouteUnavailable("Calibration measurement is not active")};
    }

    return prepareCommit(gain_db, m_active_measurement->input_device_identity, facts);
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
        common::audio::inputCalibrationMatchesIdentity(*previous_state, *current_identity))
    {
        m_calibration_state = previous_state;
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

InputCalibrationRestorePlan InputCalibrationWorkflow::prepareMeasurementRestore(
    const InputCalibrationFacts& facts) const
{
    if (!m_active_measurement.has_value())
    {
        return {};
    }

    if (!factsReadyForCalibration(facts))
    {
        return InputCalibrationRestorePlan{.kind = InputCalibrationRestoreKind::DisableLiveInput};
    }

    if (!facts.current_input_device_identity.has_value() ||
        *facts.current_input_device_identity != m_active_measurement->input_device_identity)
    {
        return InputCalibrationRestorePlan{
            .kind = InputCalibrationRestoreKind::ClearCalibrationAndClosePrompt
        };
    }

    const std::optional<common::audio::InputCalibrationState>& previous_state =
        m_active_measurement->previous_calibration_state;
    if (!previous_state.has_value() || !common::audio::inputCalibrationMatchesIdentity(
                                           *previous_state, *facts.current_input_device_identity))
    {
        return InputCalibrationRestorePlan{.kind = InputCalibrationRestoreKind::ClearCalibration};
    }

    return InputCalibrationRestorePlan{
        .kind = InputCalibrationRestoreKind::RestorePrevious,
        .previous_calibration_state = previous_state,
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
    return calibrationMatches(current_identity) ? m_calibration_state->calibration_gain.db
                                                : common::audio::defaultGainDb();
}

} // namespace rock_hero::editor::core
