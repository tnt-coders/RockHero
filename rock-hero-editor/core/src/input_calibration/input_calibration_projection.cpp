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

// Says why live input is off, or nothing while it plays. The route's own state comes first, since
// it is what the player can fix; a calibrated route then reports what the gate is doing, with the
// editor's two reasons for an unready session named instead of the shared generic sentence. A
// fault keeps its own sentence, so an unreadable store is not reported as a backend failure.
[[nodiscard]] std::string_view disabledMessageFor(
    InputCalibrationStatus status, common::audio::LiveInputMonitoringStatus monitor_status,
    bool settings_open)
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
            if (monitor_status != common::audio::LiveInputMonitoringStatus::SessionNotReady)
            {
                return common::audio::liveInputStatusText(monitor_status);
            }
            return settings_open ? "Live input is paused while the audio settings are open."
                                 : "Live input starts once the project's audio has loaded.";
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
        .disabled_message =
            std::string{disabledMessageFor(status, monitor.status(), settings_open)},
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
