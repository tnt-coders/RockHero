#include "tempo_grid_geometry.h"

#include "timeline_geometry.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace rock_hero::editor::core
{

namespace
{

// Inverse of timelineXForPosition's interior mapping: turns a drawing-width column back into
// seconds. Used only to bound the grid scan conservatively, so it may be loose; the exact per-line
// column test in visibleTempoGridLines stays the real visibility gate. Callers guarantee
// width_span > 0 so the division is safe.
double secondsAtColumn(common::core::TimeRange visible_timeline, double width_span, int column)
{
    return visible_timeline.start.seconds +
           (static_cast<double>(column) / width_span) * visible_timeline.duration().seconds;
}

// Keeps rendering and snapping on the same grid even when a caller passes corrupt spacing: both
// public entry points degrade to the whole-beat grid rather than diverging or going blank.
[[nodiscard]] common::core::Fraction normalizedTempoGridSpacing(
    common::core::Fraction spacing) noexcept
{
    return isValidTempoGridSpacing(spacing) ? spacing : common::core::Fraction{1, 1};
}

// Grid lines are addressed by index, where line k sits at (k * numerator) / denominator beats.
// Splitting that rational into a whole-beat address plus an exact fractional offset lets the tempo
// map's fractional note query resolve seconds without accumulated floating-point step error.
[[nodiscard]] double secondsAtGridLineIndex(
    const common::core::TempoMap& tempo_map, common::core::Fraction spacing, std::int64_t index)
{
    const std::int64_t beat_numerator = index * spacing.numerator;
    const std::int64_t whole_beat = beat_numerator / spacing.denominator;
    const auto offset_numerator = static_cast<int>(beat_numerator % spacing.denominator);
    const auto [measure, beat] = tempo_map.beatAtGlobalIndex(whole_beat);
    return tempo_map.secondsAtNote(
        measure, beat, common::core::Fraction{offset_numerator, spacing.denominator});
}

// Returns the last grid-line index at or before the terminal anchor beat, so scans and snaps never
// address lines past the authored map. Never negative because the terminal beat index is not.
[[nodiscard]] std::int64_t terminalGridLineIndex(
    const common::core::TempoMap& tempo_map, common::core::Fraction spacing) noexcept
{
    return tempo_map.terminalGlobalBeatIndex() * spacing.denominator / spacing.numerator;
}

// Binary-searches the first grid line at or after an absolute second. The tempo map is expected to
// be monotonic, matching visibleTempoGridLines' scan invariant.
[[nodiscard]] std::int64_t firstGridLineAtOrAfterSeconds(
    const common::core::TempoMap& tempo_map, common::core::Fraction spacing, double seconds)
{
    const std::int64_t terminal_grid_line = terminalGridLineIndex(tempo_map, spacing);
    std::int64_t first_grid_line = terminal_grid_line + 1;
    std::int64_t search_lo = 0;
    std::int64_t search_hi = terminal_grid_line;
    while (search_lo <= search_hi)
    {
        const std::int64_t mid = search_lo + (search_hi - search_lo) / 2;
        if (secondsAtGridLineIndex(tempo_map, spacing, mid) >= seconds)
        {
            first_grid_line = mid;
            search_hi = mid - 1;
        }
        else
        {
            search_lo = mid + 1;
        }
    }

    return first_grid_line;
}

} // namespace

// Pure presentation math kept in editor-core so the timeline grid stays unit-testable without JUCE.
std::vector<TempoGridLine> visibleTempoGridLines(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_spacing_beats,
    common::core::TimeRange visible_timeline, int width, int visible_x_begin, int visible_x_end)
{
    std::vector<TempoGridLine> lines;
    if (width <= 0 || visible_timeline.duration().seconds <= 0.0 ||
        visible_x_begin >= visible_x_end)
    {
        return lines;
    }

    const common::core::Fraction spacing = normalizedTempoGridSpacing(grid_spacing_beats);
    const std::int64_t terminal_grid_line = terminalGridLineIndex(tempo_map, spacing);

    // timelineXForPosition spreads the visible range across [0, width - 1], so a zero span means a
    // single-pixel canvas where the inverse is undefined. There, skip the bounding search and scan
    // every line; the exact column test below still keeps the output correct.
    const double width_span = static_cast<double>(width - 1);
    const bool can_bound_scan = width_span > 0.0;

    // One-pixel margins absorb the rounding in the forward map so the bounds never exclude a line
    // whose rounded column still lands inside the visible span.
    const double window_start_seconds =
        can_bound_scan ? secondsAtColumn(visible_timeline, width_span, visible_x_begin - 1)
                       : std::numeric_limits<double>::lowest();
    const double window_end_seconds =
        can_bound_scan ? secondsAtColumn(visible_timeline, width_span, visible_x_end)
                       : std::numeric_limits<double>::max();

    const std::int64_t first_visible_grid_line =
        firstGridLineAtOrAfterSeconds(tempo_map, spacing, window_start_seconds);

    for (std::int64_t grid_line_index = first_visible_grid_line;
         grid_line_index <= terminal_grid_line;
         ++grid_line_index)
    {
        const std::int64_t beat_numerator = grid_line_index * spacing.numerator;
        const std::int64_t whole_beat = beat_numerator / spacing.denominator;
        const auto offset_numerator = static_cast<int>(beat_numerator % spacing.denominator);
        const auto [measure, beat] = tempo_map.beatAtGlobalIndex(whole_beat);
        const double seconds = tempo_map.secondsAtNote(
            measure, beat, common::core::Fraction{offset_numerator, spacing.denominator});
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

        TempoGridLineRank rank = TempoGridLineRank::Subdivision;
        if (offset_numerator == 0)
        {
            rank = beat == 1 ? TempoGridLineRank::Measure : TempoGridLineRank::Beat;
        }

        if (!lines.empty() && lines.back().x == column)
        {
            // Several lines round onto one column when zoomed far out; keep the strongest rank so
            // measure and beat boundaries still read at low zoom, and keep that rank's musical
            // position so ruler labels match the promoted color.
            if (rank > lines.back().rank)
            {
                lines.back().measure = measure;
                lines.back().beat = beat;
                lines.back().rank = rank;
            }
            continue;
        }

        lines.push_back(TempoGridLine{.x = column, .measure = measure, .beat = beat, .rank = rank});
    }

    return lines;
}

// Finds the closest grid time by comparing the two grid lines bracketing the target. Distances are
// measured in seconds, not pixels, so the snapped time is exact and independent of zoom.
common::core::TimePosition nearestTempoGridTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_spacing_beats,
    common::core::TimePosition target)
{
    const common::core::Fraction spacing = normalizedTempoGridSpacing(grid_spacing_beats);
    const std::int64_t terminal_grid_line = terminalGridLineIndex(tempo_map, spacing);
    const std::int64_t first_after_target =
        firstGridLineAtOrAfterSeconds(tempo_map, spacing, target.seconds);

    double best_seconds = 0.0;
    double best_distance = std::numeric_limits<double>::infinity();
    const auto considerGridLine = [&](std::int64_t grid_line_index) {
        if (grid_line_index < 0 || grid_line_index > terminal_grid_line)
        {
            return;
        }

        const double seconds = secondsAtGridLineIndex(tempo_map, spacing, grid_line_index);
        const double distance = std::abs(seconds - target.seconds);
        if (distance < best_distance)
        {
            best_seconds = seconds;
            best_distance = distance;
        }
    };

    // Check the neighboring grid lines around the target time. Considering the earlier line first
    // makes exact halfway targets choose the earlier grid line instead of jumping forward. At
    // least one candidate is always in range because terminalGridLineIndex() is never negative and
    // firstGridLineAtOrAfterSeconds returns a value in [0, terminal + 1].
    considerGridLine(first_after_target - 1);
    considerGridLine(first_after_target);
    return common::core::TimePosition{best_seconds};
}

} // namespace rock_hero::editor::core
