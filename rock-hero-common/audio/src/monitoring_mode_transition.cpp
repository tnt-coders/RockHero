#include "monitoring_mode_transition.h"

namespace rock_hero::common::audio
{

// Keeps the monitoring-mode policy testable without Tracktion routing or device state.
std::optional<MonitoringFlags> monitoringFlagsForRequest(
    MonitoringFlags current, MonitorChannel channel, bool enabled, bool input_device_available)
{
    // Enabling either monitoring mode requires an input device to route from.
    if (enabled && !input_device_available)
    {
        return std::nullopt;
    }

    // The two modes are mutually exclusive: enabling one disables the other; disabling a mode
    // leaves the other untouched.
    MonitoringFlags result = current;
    bool& selected = channel == MonitorChannel::LiveInput ? result.live_input : result.calibration;
    bool& other = channel == MonitorChannel::LiveInput ? result.calibration : result.live_input;
    selected = enabled;
    if (enabled)
    {
        other = false;
    }

    return result;
}

} // namespace rock_hero::common::audio
