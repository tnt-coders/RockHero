#include "input_calibration/input_calibration_text.h"

#include <cstddef>
#include <format>
#include <variant>

namespace rock_hero::editor::core
{

std::string idleText()
{
    return "Most accurate: type your audio device's gain from the guide (?). Not listed? Choose "
           "your pickup type and click \"Measure\" to calibrate by playing.";
}

std::string guideUnavailableText()
{
    return "The calibration guide could not be opened.";
}

std::string peakTargetText(common::audio::PickupClass pickups)
{
    return std::format(
        "A hard strum on {} pickups peaks at the target when the gain is right.",
        common::audio::pickupType(pickups).name);
}

std::string measuringText(const common::audio::InputCalibrationRunning& running)
{
    const auto* const listening = std::get_if<common::audio::InputCalibrationListening>(&running);
    if (listening == nullptr)
    {
        return std::format(
            "{} {}",
            common::audio::guitarSetupInstructionText(),
            common::audio::hardStrumInstructionText());
    }

    // Whole seconds, rounded up, so the count reaches 1 s before it ends rather than 0 s.
    const auto windows_per_second =
        static_cast<std::size_t>(common::audio::inputCalibrationSampleRateHz());
    const std::size_t seconds_left =
        (listening->windows_remaining + windows_per_second - 1) / windows_per_second;
    return std::format("Keep strumming that hard. {} s left.", seconds_left);
}

std::string measuredText(double gain_db, common::audio::PickupClass pickups)
{
    return std::format(
        "Measured {} dB with {} pickups. Click Apply to save it.",
        common::audio::inputCalibrationGainText(gain_db),
        common::audio::pickupType(pickups).name);
}

} // namespace rock_hero::editor::core
