#include "input_calibration/input_calibration_projection.h"

#include <optional>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <string>
#include <string_view>

namespace rock_hero::editor::core
{

namespace
{

// Words the editor's four-way status with the one shared authority. A fault keeps its own
// sentence, so an unreadable store is not reported as a backend failure.
[[nodiscard]] std::string_view disabledMessageFor(
    InputCalibrationStatus status, common::audio::LiveInputMonitoringStatus monitor_status)
{
    switch (status)
    {
        case InputCalibrationStatus::NoActiveInputDevice:
        {
            return common::audio::liveInputStatusText(
                common::audio::LiveInputMonitoringStatus::NoInputDevice);
        }
        case InputCalibrationStatus::MissingCalibration:
        {
            return common::audio::liveInputStatusText(
                common::audio::LiveInputMonitoringStatus::MissingCalibration);
        }
        case InputCalibrationStatus::Unavailable:
        {
            return common::audio::liveInputStatusText(monitor_status);
        }
        case InputCalibrationStatus::Calibrated:
        {
            return {};
        }
    }

    return {};
}

} // namespace

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
    const common::audio::LiveInputMonitor& monitor,
    const std::optional<common::audio::InputDeviceIdentity>& prompt_route, bool settings_open)
{
    const InputCalibrationStatus status = inputCalibrationStatusFor(monitor);

    InputCalibrationProjection projection{
        .status = status,
        .calibrate_enabled = monitor.route().has_value() && !settings_open,
        .audio_device_settings_enabled = !prompt_route.has_value() && !settings_open,
        .disabled_message = std::string{disabledMessageFor(status, monitor.status())},
        .prompt = std::nullopt,
    };

    if (prompt_route.has_value())
    {
        const std::optional<common::audio::InputCalibrationState>& calibration =
            monitor.calibration();
        projection.prompt = InputCalibrationPrompt{
            .route = *prompt_route,
            .stored_gain_db = calibration.has_value()
                                  ? std::optional{calibration->calibration_gain.db}
                                  : std::nullopt,
        };
    }

    return projection;
}

} // namespace rock_hero::editor::core
