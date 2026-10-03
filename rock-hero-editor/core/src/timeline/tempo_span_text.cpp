#include "timeline/tempo_span_text.h"

#include <cmath>
#include <format>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Absorbs floating-point error in the span comparison, far below the millisecond being tested.
constexpr double g_span_epsilon = 1.0e-9;

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
    const double seconds = anchors[span + 1].seconds - anchors[span].seconds;
    if (quarters <= 0.0 || seconds <= 0.0)
    {
        return {};
    }

    // Two millisecond-rounded ends leave the stored span uncertain by one grid step, so a value
    // whose span lies within that step describes the same stored map: print the shortest one.
    const double bpm = quarters / seconds * 60.0;
    double scale = 1.0;
    for (int decimals = 0; decimals < g_max_tempo_decimals; ++decimals)
    {
        const double candidate = std::round(bpm * scale) / scale;
        if (candidate > 0.0 && std::abs(quarters * 60.0 / candidate - seconds) <=
                                   common::core::g_anchor_seconds_step + g_span_epsilon)
        {
            return std::format("{:.{}f}", candidate, decimals);
        }
        scale *= 10.0;
    }
    return std::format("{:.{}f}", bpm, g_max_tempo_decimals);
}

} // namespace rock_hero::editor::core
