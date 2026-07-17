#include "tab/tab_lane_layout.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace rock_hero::common::ui
{

namespace
{

// Notes smaller than this cannot fit readable fret numbers and drop to bare markers.
constexpr float g_min_note_height_for_text{9.0f};

} // namespace

// The chart's string count floors the lane count so a user minimum can only add empty lanes.
int tabDisplayedStringCount(int chart_string_count, int minimum_displayed_strings) noexcept
{
    if (chart_string_count <= 0)
    {
        return 0;
    }

    return std::max(chart_string_count, minimum_displayed_strings);
}

// Standard tablature orientation: highest string on top, lowest on the bottom. Hosts size the
// bounds proportionally to the string count, so evenly dividing the height yields identical
// per-lane spacing at every count.
float tabLaneCenterY(
    int displayed_string, int displayed_string_count, float bounds_y, float bounds_height) noexcept
{
    const float lane_height = bounds_height / static_cast<float>(displayed_string_count);
    const auto lane_index = static_cast<float>(displayed_string_count - displayed_string);
    return bounds_y + (lane_index + 0.5f) * lane_height;
}

// Maps a timeline time onto the lane's horizontal axis.
float TabLaneGeometry::x(double seconds) const noexcept
{
    const double duration = visible_timeline.duration().seconds;
    return static_cast<float>(
        (seconds - visible_timeline.start.seconds) / duration * static_cast<double>(bounds_width));
}

// Vertical lane center for a chart string, accounting for extra user lanes below the chart.
float TabLaneGeometry::laneY(int chart_string) const noexcept
{
    return tabLaneCenterY(chart_string + extra_lanes, displayed_count, bounds_y, bounds_height);
}

TabLaneGeometry makeTabLaneGeometry(
    float bounds_x, float bounds_y, float bounds_width, float bounds_height,
    common::core::TimeRange visible_timeline, int displayed_count, int chart_string_count,
    TabLaneStyle style)
{
    TabLaneGeometry geometry;
    geometry.visible_timeline = visible_timeline;
    geometry.bounds_x = bounds_x;
    geometry.bounds_y = bounds_y;
    geometry.bounds_width = bounds_width;
    geometry.bounds_height = bounds_height;
    geometry.displayed_count = displayed_count;
    geometry.extra_lanes = displayed_count - chart_string_count;
    // Lanes evenly fill the bounds, which the host sizes proportionally to the count; this
    // matches tabLaneCenterY, so note height stays at the reference-density value whatever the
    // count.
    geometry.lane_height = bounds_height / static_cast<float>(displayed_count);
    geometry.note_height = std::min(style.max_note_height, geometry.lane_height / 1.5f);
    // Charter keeps the tail height odd so the tail centers on the string line.
    const auto odd = [](float value) {
        const int rounded = static_cast<int>(value);
        return static_cast<float>(rounded % 2 == 0 ? rounded + 1 : rounded);
    };
    geometry.tail_height = odd(geometry.note_height * 3.0f / 4.0f);
    geometry.tail_edge_size = std::max(1.0f, geometry.tail_height / 8.0f);
    geometry.tremolo_size = std::max(2.0f, geometry.tail_height / 6.0f);
    geometry.max_note_height = style.max_note_height;
    geometry.draw_text = geometry.note_height >= g_min_note_height_for_text;
    return geometry;
}

TailSpan tailSpan(const TabLaneGeometry& geometry, float center_y) noexcept
{
    return TailSpan{
        .top = center_y - geometry.tail_height / 3.0f,
        .bottom = center_y + geometry.tail_height / 3.0f + 1.0f,
    };
}

// Sorted starts bound the range's end; the non-decreasing prefix maximum of sustain ends bounds
// its start, because every note before the first index whose running maximum reaches the span
// ends strictly before the span.
std::pair<std::size_t, std::size_t> tabVisibleNoteRange(
    const std::vector<common::core::TabNoteView>& notes,
    const std::vector<double>& prefix_max_end_seconds, double span_start_seconds,
    double span_end_seconds) noexcept
{
    const auto begin_it = std::ranges::lower_bound(prefix_max_end_seconds, span_start_seconds);
    const auto end_it = std::ranges::upper_bound(
        notes, span_end_seconds, std::ranges::less{}, [](const common::core::TabNoteView& note) {
            return note.start_seconds;
        });

    const auto first =
        static_cast<std::size_t>(std::distance(prefix_max_end_seconds.begin(), begin_it));
    const auto last = static_cast<std::size_t>(std::distance(notes.begin(), end_it));
    return {std::min(first, last), last};
}

std::vector<double> tabPrefixMaxEndSeconds(const std::vector<common::core::TabNoteView>& notes)
{
    std::vector<double> prefix_max_end_seconds;
    prefix_max_end_seconds.reserve(notes.size());
    double running_max = -std::numeric_limits<double>::infinity();
    for (const common::core::TabNoteView& note : notes)
    {
        running_max = std::max(running_max, note.end_seconds);
        prefix_max_end_seconds.push_back(running_max);
    }
    return prefix_max_end_seconds;
}

} // namespace rock_hero::common::ui
