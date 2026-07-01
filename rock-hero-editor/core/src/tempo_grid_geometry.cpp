#include "tempo_grid_geometry.h"

#include "timeline_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace rock_hero::editor::core
{

namespace
{

// Inverse of timelineXForPosition's interior mapping: turns a drawing-width column back into
// seconds. Used only to bound the beat scan conservatively, so it may be loose; the exact per-line
// column test in visibleTempoGridLines stays the real visibility gate. Callers guarantee
// width_span > 0 so the division is safe.
double secondsAtColumn(common::core::TimeRange visible_timeline, double width_span, int column)
{
    return visible_timeline.start.seconds +
           (static_cast<double>(column) / width_span) * visible_timeline.duration().seconds;
}

// Converts a global beat index into absolute seconds through the tempo map's public addressing
// API, keeping callers independent of tempo-map storage details.
[[nodiscard]] double secondsAtIndex(
    const common::core::TempoMap& tempo_map, std::int64_t index) noexcept
{
    const auto [measure, beat] = tempo_map.beatAtGlobalIndex(index);
    return tempo_map.secondsAtBeat(measure, beat);
}

// Binary-searches the first beat at or after an absolute second. The tempo map is expected to be
// monotonic, matching visibleTempoGridLines' scan invariant.
[[nodiscard]] std::int64_t firstBeatAtOrAfterSeconds(
    const common::core::TempoMap& tempo_map, double seconds)
{
    const std::int64_t terminal_beat = tempo_map.terminalGlobalBeatIndex();
    std::int64_t first_beat = terminal_beat + 1;
    std::int64_t search_lo = 0;
    std::int64_t search_hi = terminal_beat;
    while (search_lo <= search_hi)
    {
        const std::int64_t mid = search_lo + (search_hi - search_lo) / 2;
        if (secondsAtIndex(tempo_map, mid) >= seconds)
        {
            first_beat = mid;
            search_hi = mid - 1;
        }
        else
        {
            search_lo = mid + 1;
        }
    }

    return first_beat;
}

// Maps one global beat to its rounded grid column, rejecting beats outside the visible timeline.
[[nodiscard]] std::optional<int> columnForBeatIndex(
    const common::core::TempoMap& tempo_map, common::core::TimeRange visible_timeline, int width,
    std::int64_t beat_index)
{
    if (beat_index < 0 || beat_index > tempo_map.terminalGlobalBeatIndex())
    {
        return std::nullopt;
    }

    const auto x = timelineXForPosition(
        common::core::TimePosition{secondsAtIndex(tempo_map, beat_index)},
        visible_timeline,
        width,
        TimelinePositionClamping::RejectOutsideVisibleRange);
    if (!x.has_value())
    {
        return std::nullopt;
    }

    return static_cast<int>(std::round(*x));
}

} // namespace

// Pure presentation math kept in editor-core so the timeline grid stays unit-testable without JUCE.
std::vector<TempoGridLine> visibleTempoGridLines(
    const common::core::TempoMap& tempo_map, common::core::TimeRange visible_timeline, int width,
    int visible_x_begin, int visible_x_end)
{
    std::vector<TempoGridLine> lines;
    if (width <= 0 || visible_timeline.duration().seconds <= 0.0 ||
        visible_x_begin >= visible_x_end)
    {
        return lines;
    }

    const std::int64_t terminal_beat = tempo_map.terminalGlobalBeatIndex();

    // timelineXForPosition spreads the visible range across [0, width - 1], so a zero span means a
    // single-pixel canvas where the inverse is undefined. There, skip the bounding search and scan
    // every beat; the exact column test below still keeps the output correct.
    const double width_span = static_cast<double>(width - 1);
    const bool can_bound_scan = width_span > 0.0;

    // One-pixel margins absorb the rounding in the forward map so the bounds never exclude a beat
    // whose rounded column still lands inside the visible span.
    const double window_start_seconds =
        can_bound_scan ? secondsAtColumn(visible_timeline, width_span, visible_x_begin - 1)
                       : std::numeric_limits<double>::lowest();
    const double window_end_seconds =
        can_bound_scan ? secondsAtColumn(visible_timeline, width_span, visible_x_end)
                       : std::numeric_limits<double>::max();

    const std::int64_t first_visible_beat =
        firstBeatAtOrAfterSeconds(tempo_map, window_start_seconds);

    for (std::int64_t beat_index = first_visible_beat; beat_index <= terminal_beat; ++beat_index)
    {
        const auto [measure, beat] = tempo_map.beatAtGlobalIndex(beat_index);
        const double seconds = secondsAtIndex(tempo_map, beat_index);
        if (seconds > window_end_seconds)
        {
            break;
        }

        const auto x = timelineXForPosition(
            common::core::TimePosition{seconds},
            visible_timeline,
            width,
            TimelinePositionClamping::RejectOutsideVisibleRange);
        if (!x.has_value())
        {
            continue;
        }

        const int column = static_cast<int>(std::round(*x));
        if (column < visible_x_begin || column >= visible_x_end)
        {
            continue;
        }

        const bool measure_start = beat == 1;
        if (!lines.empty() && lines.back().x == column)
        {
            // Several beats round onto one column when zoomed far out; keep the downbeat color so
            // measure boundaries still read at low zoom. If a later beat in the same column is the
            // first downbeat, keep that musical position too so ruler labels match the color.
            if (measure_start && !lines.back().measure_start)
            {
                lines.back().measure = measure;
                lines.back().beat = beat;
                lines.back().measure_start = true;
            }
            continue;
        }

        lines.push_back(
            TempoGridLine{
                .x = column, .measure = measure, .beat = beat, .measure_start = measure_start
            });
    }

    return lines;
}

// Finds the closest visual beat-grid column to a clicked target column.
std::optional<int> nearestTempoGridLineX(
    const common::core::TempoMap& tempo_map, common::core::TimeRange visible_timeline, int width,
    int target_x)
{
    if (width <= 0 || visible_timeline.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    const int clamped_target_x = std::clamp(target_x, 0, width - 1);
    const double width_span = static_cast<double>(width - 1);
    const double target_seconds =
        width_span > 0.0 ? secondsAtColumn(visible_timeline, width_span, clamped_target_x)
                         : visible_timeline.start.seconds;
    const std::int64_t first_after_target = firstBeatAtOrAfterSeconds(tempo_map, target_seconds);

    std::optional<int> best_column;
    int best_distance = std::numeric_limits<int>::max();
    const auto considerBeat = [&](std::int64_t beat_index) {
        const std::optional<int> column =
            columnForBeatIndex(tempo_map, visible_timeline, width, beat_index);
        if (!column.has_value())
        {
            return;
        }

        const int distance = std::abs(*column - clamped_target_x);
        if (distance < best_distance)
        {
            best_column = column;
            best_distance = distance;
        }
    };

    // Check the neighboring beats around the click time. Searching the left candidate first makes
    // exact halfway clicks choose the earlier grid line instead of jumping forward.
    considerBeat(first_after_target - 1);
    considerBeat(first_after_target);
    return best_column;
}

} // namespace rock_hero::editor::core
