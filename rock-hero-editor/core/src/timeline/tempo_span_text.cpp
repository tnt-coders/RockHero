#include "timeline/tempo_span_text.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The most decimals the chip prints.
constexpr int g_max_tempo_decimals = 2;

} // namespace

std::string tempoSpanBpmText(const common::core::TempoMap& tempo_map, std::size_t span)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    if (span + 1 >= anchors.size())
    {
        return {};
    }

    const double quarters = tempo_map.spanQuarterLength(span);
    const std::int64_t milliseconds = anchors[span + 1].milliseconds - anchors[span].milliseconds;
    if (quarters <= 0.0 || milliseconds <= 0)
    {
        return {};
    }

    // Two whole-millisecond ends leave the stored span uncertain by one millisecond, so any value
    // whose span lies within that millisecond describes the same stored map; the shortest prints.
    const auto span_milliseconds = static_cast<double>(milliseconds);
    const double bpm = quarters * 60000.0 / span_milliseconds;
    double scale = 1.0;
    for (int decimals = 0; decimals < g_max_tempo_decimals; ++decimals)
    {
        const double candidate = std::round(bpm * scale) / scale;
        if (candidate > 0.0 && std::abs(quarters * 60000.0 / candidate - span_milliseconds) <= 1.0)
        {
            return std::format("{:.{}f}", candidate, decimals);
        }
        scale *= 10.0;
    }
    return std::format("{:.{}f}", bpm, g_max_tempo_decimals);
}

} // namespace rock_hero::editor::core
