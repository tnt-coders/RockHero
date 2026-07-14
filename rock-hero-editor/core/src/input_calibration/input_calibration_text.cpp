#include "input_calibration/input_calibration_text.h"

#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/shared/gain.h>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] bool contextReadyForCalibration(
    const common::audio::InputCalibrationWorkflow::Context& context) noexcept
{
    return context.project_audio_ready && context.arrangement_loaded;
}

// Reason reflecting only route identity and calibration match, deliberately ignoring the
// settings-open and session-ready early-outs that evaluateMonitoring reports first. The
// signal-chain status keeps showing a route's calibration while device settings are open, so it
// must be derived from this identity/calibration-only reason rather than the ordered gate result.
[[nodiscard]] common::audio::MonitoringDisabledReason statusReasonFor(
    const common::audio::InputCalibrationWorkflow& workflow,
    const std::optional<common::audio::InputDeviceIdentity>& identity)
{
    if (!identity.has_value())
    {
        return common::audio::MonitoringDisabledReason::NoInputDevice;
    }

    if (!workflow.calibrationMatches(identity))
    {
        return common::audio::MonitoringDisabledReason::MissingCalibration;
    }

    return common::audio::MonitoringDisabledReason::None;
}

// Gain shown by the calibration prompt: the stored matching-route gain, else the neutral default.
[[nodiscard]] double promptGainDb(
    const common::audio::InputCalibrationWorkflow& workflow,
    const std::optional<common::audio::InputDeviceIdentity>& identity)
{
    const std::optional<common::audio::InputCalibrationState> calibration =
        workflow.activeCalibrationState();
    if (!workflow.calibrationMatches(identity) || !calibration.has_value())
    {
        return common::audio::defaultGainDb();
    }

    return calibration->calibration_gain.db;
}

} // namespace

InputCalibrationStatus inputCalibrationStatusFor(
    common::audio::MonitoringDisabledReason reason, bool backend_available)
{
    switch (reason)
    {
        case common::audio::MonitoringDisabledReason::None:
        {
            return backend_available ? InputCalibrationStatus::Calibrated
                                     : InputCalibrationStatus::Unavailable;
        }
        case common::audio::MonitoringDisabledReason::MissingCalibration:
        case common::audio::MonitoringDisabledReason::CalibrationRouteMismatch:
        {
            return InputCalibrationStatus::MissingCalibration;
        }
        case common::audio::MonitoringDisabledReason::AudioDeviceSettingsOpen:
        case common::audio::MonitoringDisabledReason::SessionNotReady:
        case common::audio::MonitoringDisabledReason::NoInputDevice:
        {
            return InputCalibrationStatus::NoActiveInputDevice;
        }
        case common::audio::MonitoringDisabledReason::BackendUnavailable:
        case common::audio::MonitoringDisabledReason::CalibrationStoreUnavailable:
        {
            // Post-I/O outcomes the downstream service reports; the editor treats them as a
            // present-but-unusable calibration to match the backend-unavailable status.
            return InputCalibrationStatus::Unavailable;
        }
    }

    return InputCalibrationStatus::NoActiveInputDevice;
}

std::string inputCalibrationDisabledMessageFor(InputCalibrationStatus status)
{
    switch (status)
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

InputCalibrationViewSlice makeInputCalibrationViewState(
    const common::audio::InputCalibrationWorkflow& workflow,
    const common::audio::InputCalibrationWorkflow::Context& context)
{
    const std::optional<common::audio::InputDeviceIdentity>& identity =
        context.current_input_device_identity;
    const bool ready = contextReadyForCalibration(context);
    const bool settings_open = workflow.audioDeviceSettingsOpen();
    const bool prompt_visible = workflow.promptVisible();
    const bool backend_available = workflow.backendAvailable();
    const bool audition_available = ready && workflow.calibrationMatches(identity) &&
                                    backend_available && !prompt_visible && !settings_open;

    const InputCalibrationStatus status =
        inputCalibrationStatusFor(statusReasonFor(workflow, identity), backend_available);
    const std::string disabled_message = inputCalibrationDisabledMessageFor(status);

    InputCalibrationViewSlice slice{
        .status = status,
        .calibrate_enabled = ready && !settings_open && identity.has_value(),
        .live_input_audition_available = audition_available,
        .audio_device_settings_enabled = !prompt_visible && !settings_open,
        .disabled_message = audition_available ? std::string{} : disabled_message,
        .prompt = std::nullopt,
    };

    if (prompt_visible)
    {
        slice.prompt = InputCalibrationPrompt{
            .message = disabled_message,
            .input_gain_db = promptGainDb(workflow, identity),
        };
    }

    return slice;
}

} // namespace rock_hero::editor::core
