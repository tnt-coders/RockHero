#include "input_calibration/input_calibration_text.h"

#include <compare>
#include <cstddef>
#include <format>
#include <variant>

namespace rock_hero::editor::core
{

std::string signedGainText(double gain_db)
{
    const double shown_db = common::audio::quantizeInputCalibrationGainDb(gain_db);
    if (std::is_eq(shown_db <=> 0.0))
    {
        return "0.0";
    }
    return std::format("{:+.1f}", shown_db);
}

std::string idleText()
{
    return "Most accurate: type your audio device's gain from the guide (?). Not listed? Choose "
           "your pickup type and click \"Calibrate\" to measure it by playing.";
}

std::string guideMissingText()
{
    return "The calibration guide is not installed.";
}

std::string measuringText(const common::audio::InputCalibrationRunning& running)
{
    const auto* const listening = std::get_if<common::audio::InputCalibrationListening>(&running);
    if (listening == nullptr)
    {
        return "Plug the guitar straight into the instrument input, no pedals. Volume and tone all "
               "the way up, one pickup selected. Then play as hard as you play in a song, on all "
               "strings.";
    }

    // Whole seconds, rounded up, so the count reaches 1 s before it ends rather than 0 s.
    const auto windows_per_second =
        static_cast<std::size_t>(common::audio::inputCalibrationSampleRateHz());
    const std::size_t seconds_left =
        (listening->windows_remaining + windows_per_second - 1) / windows_per_second;
    return std::format("Keep playing that hard. {} s left.", seconds_left);
}

std::string measuredText(double gain_db, common::audio::PickupClass pickups)
{
    return std::format(
        "Measured {} dB with {} pickups. Click Apply to save it.",
        signedGainText(gain_db),
        common::audio::pickupType(pickups).name);
}

} // namespace rock_hero::editor::core
