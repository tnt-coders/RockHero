#include "input_calibration/input_calibration_projection.h"

#include "input_calibration/input_calibration_text.h"

#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/shared/gain.h>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] bool contextReadyForCalibration(
    common::audio::LiveInputMonitoringContext context) noexcept
{
    // Calibration only needs the live input path up; an arrangement gates active processed
    // monitoring, not the raw measurement. live_input_ready reports exactly that path — an open
    // device with an input route — so calibration is available with no project loaded.
    return context.live_input_ready;
}

// Reason reflecting only route identity and calibration match, deliberately ignoring the
// settings-open and session-ready early-outs that the gate reports first. The signal-chain status
// keeps showing a route's calibration while device settings are open, so it must be derived from
// this identity/calibration-only reason rather than the ordered gate result.
//
// The two uncalibrated reasons are told apart exactly as the shared workflow's own gate tells them
// apart (InputCalibrationWorkflow::evaluateMonitoring): no calibration held at all, versus one held
// against a route that is no longer the current one.
[[nodiscard]] common::audio::LiveInputMonitoringDisabledReason statusReasonFor(
    const common::audio::LiveInputMonitor& monitor,
    const std::optional<common::audio::InputDeviceIdentity>& identity)
{
    if (!identity.has_value())
    {
        return common::audio::LiveInputMonitoringDisabledReason::NoInputDevice;
    }

    if (!monitor.activeCalibrationState().has_value())
    {
        return common::audio::LiveInputMonitoringDisabledReason::MissingCalibration;
    }

    if (!monitor.calibrationMatchesCurrentRoute())
    {
        return common::audio::LiveInputMonitoringDisabledReason::CalibrationRouteMismatch;
    }

    return common::audio::LiveInputMonitoringDisabledReason::None;
}

// Gain of the calibration held for the current route, absent when none is held for it.
[[nodiscard]] std::optional<double> calibrationGainDbFor(
    const common::audio::LiveInputMonitor& monitor)
{
    const std::optional<common::audio::InputCalibrationState> calibration =
        monitor.activeCalibrationState();
    if (!monitor.calibrationMatchesCurrentRoute() || !calibration.has_value())
    {
        return std::nullopt;
    }

    return calibration->calibration_gain.db;
}

} // namespace

InputCalibrationStatus inputCalibrationStatusFor(
    common::audio::LiveInputMonitoringDisabledReason reason, bool backend_available)
{
    switch (reason)
    {
        case common::audio::LiveInputMonitoringDisabledReason::None:
        {
            return backend_available ? InputCalibrationStatus::Calibrated
                                     : InputCalibrationStatus::Unavailable;
        }
        case common::audio::LiveInputMonitoringDisabledReason::MissingCalibration:
        {
            return InputCalibrationStatus::MissingCalibration;
        }
        case common::audio::LiveInputMonitoringDisabledReason::CalibrationRouteMismatch:
        {
            return InputCalibrationStatus::CalibrationRouteMismatch;
        }
        case common::audio::LiveInputMonitoringDisabledReason::AudioDeviceSettingsOpen:
        case common::audio::LiveInputMonitoringDisabledReason::SessionNotReady:
        case common::audio::LiveInputMonitoringDisabledReason::NoInputDevice:
        {
            return InputCalibrationStatus::NoActiveInputDevice;
        }
        case common::audio::LiveInputMonitoringDisabledReason::BackendUnavailable:
        case common::audio::LiveInputMonitoringDisabledReason::CalibrationStoreUnavailable:
        {
            // Post-I/O outcomes the downstream service reports; the editor treats them as a
            // present-but-unusable calibration to match the backend-unavailable status.
            return InputCalibrationStatus::Unavailable;
        }
    }

    return InputCalibrationStatus::NoActiveInputDevice;
}

InputCalibrationProjection makeInputCalibrationProjection(
    const common::audio::LiveInputMonitor& monitor,
    common::audio::LiveInputMonitoringContext context)
{
    const std::optional<common::audio::InputDeviceIdentity> identity =
        monitor.currentInputDeviceIdentity();
    const bool ready = contextReadyForCalibration(context);
    const bool settings_open = monitor.audioDeviceSettingsOpen();
    const bool prompt_visible = monitor.promptVisible();
    const bool backend_available = monitor.backendAvailable();
    const bool audition_available = ready && monitor.calibrationMatchesCurrentRoute() &&
                                    backend_available && !prompt_visible && !settings_open;

    const InputCalibrationStatus status =
        inputCalibrationStatusFor(statusReasonFor(monitor, identity), backend_available);
    const std::string disabled_message = inputCalibrationDisabledMessageFor(status);
    const std::optional<double> calibration_gain_db = calibrationGainDbFor(monitor);

    InputCalibrationProjection projection{
        .status = status,
        .calibration_gain_db = calibration_gain_db,
        // Deliberately blind to the settings window: calibration is reached FROM that window, so a
        // settings-open term would leave its own button permanently disabled. The two windows stay
        // exclusive by hand-off instead -- the button applies the staged route and closes the
        // settings window before the prompt opens, and the workflow refuses a prompt while the
        // window is still open.
        .calibrate_enabled = ready && identity.has_value(),
        .live_input_audition_available = audition_available,
        .audio_device_settings_enabled = !prompt_visible && !settings_open,
        .disabled_message = audition_available ? std::string{} : disabled_message,
        .prompt = std::nullopt,
    };

    if (prompt_visible)
    {
        projection.prompt = InputCalibrationPrompt{
            .message = disabled_message,
            // The prompt opens on the route's own gain where it has one, and on the neutral
            // default where it does not.
            .input_gain_db = calibration_gain_db.value_or(common::audio::defaultGainDb()),
        };
    }

    return projection;
}

} // namespace rock_hero::editor::core
