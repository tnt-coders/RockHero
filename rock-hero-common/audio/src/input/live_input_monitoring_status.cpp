#include "input/live_input_monitoring_status.h"

namespace rock_hero::common::audio
{

std::string_view liveInputStatusText(LiveInputMonitoringStatus status) noexcept
{
    switch (status)
    {
        case LiveInputMonitoringStatus::Active:
        {
            return {};
        }
        case LiveInputMonitoringStatus::Measuring:
        {
            return "Input calibration is in progress.";
        }
        case LiveInputMonitoringStatus::CalibrationStoreUnavailable:
        {
            return "Input calibration could not be read.";
        }
        case LiveInputMonitoringStatus::SessionNotReady:
        {
            return "Live input is not ready.";
        }
        case LiveInputMonitoringStatus::NoInputDevice:
        {
            return "No audio input device.";
        }
        case LiveInputMonitoringStatus::MissingCalibration:
        {
            return "Input calibration required.";
        }
        case LiveInputMonitoringStatus::BackendUnavailable:
        {
            return "Live input backend unavailable.";
        }
    }

    return "Live input is off.";
}

} // namespace rock_hero::common::audio
