#include "input_calibration/input_calibration_projection.h"

#include "input_calibration/input_calibration_text.h"

#include <optional>
#include <rock_hero/common/audio/shared/gain.h>
#include <string>

namespace rock_hero::editor::core
{

InputCalibrationStatus inputCalibrationStatusFor(const common::audio::LiveInputMonitor& monitor)
{
    if (!monitor.route().has_value())
    {
        return InputCalibrationStatus::NoActiveInputDevice;
    }
    if (common::audio::isLiveInputFault(monitor.status()))
    {
        return InputCalibrationStatus::Unavailable;
    }

    return monitor.calibration().has_value() ? InputCalibrationStatus::Calibrated
                                             : InputCalibrationStatus::MissingCalibration;
}

InputCalibrationProjection makeInputCalibrationProjection(
    const common::audio::LiveInputMonitor& monitor, bool prompt_open, bool settings_open)
{
    const InputCalibrationStatus status = inputCalibrationStatusFor(monitor);
    const bool audition_available =
        status == InputCalibrationStatus::Calibrated && !prompt_open && !settings_open;

    InputCalibrationProjection projection{
        .status = status,
        .calibrate_enabled = monitor.route().has_value() && !settings_open,
        .audio_device_settings_enabled = !prompt_open && !settings_open,
        .disabled_message =
            audition_available ? std::string{} : inputCalibrationDisabledMessageFor(status),
        .prompt = std::nullopt,
    };

    if (prompt_open)
    {
        // The prompt opens on the route's stored gain, else the neutral default.
        const std::optional<common::audio::InputCalibrationState>& calibration =
            monitor.calibration();
        projection.prompt = InputCalibrationPrompt{
            .input_gain_db = calibration.has_value() ? calibration->calibration_gain.db
                                                     : common::audio::defaultGainDb(),
        };
    }

    return projection;
}

} // namespace rock_hero::editor::core
