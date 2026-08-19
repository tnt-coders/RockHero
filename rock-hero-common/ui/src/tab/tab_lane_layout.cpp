#include "tab/tab_lane_layout.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/common/core/shared/displayed_strings.h>

namespace rock_hero::common::ui
{

namespace
{

// Notes smaller than this cannot fit readable fret numbers and drop to bare markers.
constexpr float g_min_note_height_for_text{9.0f};

} // namespace

// Standard tablature orientation: highest string on top, lowest on the bottom. Hosts size the
// bounds proportionally to the string count, so evenly dividing the height yields identical
// per-lane spacing at every count.
float tabLaneCenterY(
    int displayed_string, int displayed_string_count, float bounds_y, float bounds_height) noexcept
{
    const float lane_height = bounds_height / static_cast<float>(displayed_string_count);
    const auto lane_index = static_cast<float>(displayed_string_count - displayed_string);
    // Snapped to a pixel ROW CENTRE, which is what makes every mark this lane draws symmetric
    // about the string line. A pixel row spans [N, N+1], so a row is the mirror of another row
    // only when 2 * center_y is a whole number; the shipped editor divides 237 px over six lanes
    // for 39.5 px each, which lands EVERY lane centre on a .25 or .75 boundary and leaves no row
    // with a mirror at all. The tail's two rails then rasterise to different row counts — two
    // solid rows on one side, three on the other, a 93.6-count difference in the outermost row —
    // which reads as one border being crisper than the other, and the head and the accent halo
    // inherit the same phase error.
    //
    // The string line does NOT move: drawStringLines already snapped its own one-pixel line with
    // `(int)y`, which is exactly `floor(c) + 0.5` as a row centre. This moves the tail and the
    // head onto the row the renderer was already drawing, rather than the reverse, and lets that
    // second snapping authority be deleted.
    return std::floor(bounds_y + ((lane_index + 0.5f) * lane_height)) + 0.5f;
}

// Maps a timeline time onto the lane's horizontal axis, measured from the bounds' left edge just
// as laneY measures from its top: a host that places the lane inside a larger component reads
// positions in that component's own space.
float TabLaneGeometry::x(double seconds) const noexcept
{
    const double duration = visible_timeline.duration().seconds;
    return bounds_x + static_cast<float>(
                          (seconds - visible_timeline.start.seconds) / duration *
                          static_cast<double>(bounds_width));
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
    // A WHOLE number of rows, so both rails rasterise identically instead of one landing on a
    // half-covered row. At the shipped tail height this rounds 2.375 down to 2, which is what
    // widens the technique band below (tailInterior grows 10.9%, the vibrato sine's swing 14.4%);
    // std::ceil is the knob if the band reads too tall, giving a 3 px rail and a 9.2% narrower
    // band instead.
    geometry.tail_edge_size = std::max(1.0f, std::round(geometry.tail_height / 8.0f));
    geometry.tremolo_size = std::max(2.0f, geometry.tail_height / 6.0f);
    geometry.max_note_height = style.max_note_height;
    geometry.draw_text = geometry.note_height >= g_min_note_height_for_text;
    return geometry;
}

TailSpan tailSpan(const TabLaneGeometry& geometry, float center_y) noexcept
{
    // The tail's whole outer envelope, rails included, symmetric about the string line. Charter
    // spells this as an asymmetric body span plus a one-pixel border overhang at the top; folding
    // the overhang in here keeps the symmetry in ONE place instead of asking every consumer to
    // re-balance it (the tremolo band and the hit-test rectangle both sagged a pixel low when
    // they didn't).
    // Rounded to a HALF pixel so that, with the lane centre on a row centre (C1 in
    // tabLaneCenterY), both span edges land on whole pixel boundaries and the two rails cover
    // identical rows. 19/3 + 1 = 7.3333 becomes 7.5 at the shipped tail height.
    const float half = std::round(((geometry.tail_height / 3.0f) + 1.0f) * 2.0f) / 2.0f;
    return TailSpan{
        .top = center_y - half,
        .bottom = center_y + half,
    };
}

} // namespace rock_hero::common::ui
