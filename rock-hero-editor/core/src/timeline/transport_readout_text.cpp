#include "transport_readout_text.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

namespace rock_hero::editor::core
{

// Splits one rounded millisecond total so the fields stay carry-consistent: 59.9996 seconds
// rounds to 60000ms and rolls into the next minute instead of showing a 1000ms field.
std::string timelineTimeText(double seconds)
{
    const double clamped_seconds = std::max(0.0, seconds);
    const auto total_milliseconds =
        static_cast<std::int64_t>(std::llround(clamped_seconds * 1000.0));
    const std::int64_t hours = total_milliseconds / 3600000;
    const std::int64_t minutes = (total_milliseconds / 60000) % 60;
    const std::int64_t remainder = total_milliseconds % 60000;
    if (hours > 0)
    {
        return std::format(
            "{}:{:02}:{:02}:{:03}", hours, minutes, remainder / 1000, remainder % 1000);
    }

    return std::format("{}:{:02}:{:03}", minutes, remainder / 1000, remainder % 1000);
}

// Produces the musical half of the transport readout from the tempo map's seconds-to-beat inverse.
std::string beatPositionText(const common::core::TempoMap& tempo_map, double seconds)
{
    // Quantize to display hundredths BEFORE splitting off the whole beat: the seconds-to-beat
    // inverse of a downbeat produced by the forward beat-to-seconds map can come back as
    // 3.9999... through anchor-span interpolation rounding, and flooring that raw would show
    // 1.4.99 for what is exactly measure 2's start.
    const auto total_hundredths =
        static_cast<std::int64_t>(std::llround(tempo_map.beatPositionAtSeconds(seconds) * 100.0));
    const auto [measure, beat] = tempo_map.beatAtGlobalIndex(total_hundredths / 100);
    return std::format("{}.{}.{:02}", measure, beat, total_hundredths % 100);
}

} // namespace rock_hero::editor::core
