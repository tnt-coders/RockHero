#include "tab/tab_lane_layout.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <rock_hero/common/core/chart/bend_travel.h>
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

double TabLaneGeometry::secondsPerPixel() const noexcept
{
    return visible_timeline.duration().seconds / static_cast<double>(bounds_width);
}

// Vertical lane center for a chart string, accounting for extra user lanes below the chart.
float TabLaneGeometry::laneY(int chart_string) const noexcept
{
    return tabLaneCenterY(
        common::core::displayedLane(chart_string, extra_lanes),
        displayed_count,
        bounds_y,
        bounds_height);
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

TailInterior tailInterior(const TabLaneGeometry& geometry, const float center_y) noexcept
{
    const TailSpan span = tailSpan(geometry, center_y);
    return TailInterior{
        .top = span.top + geometry.tail_edge_size, .bottom = span.bottom - geometry.tail_edge_size
    };
}

float bendCurveY(
    const TabLaneGeometry& geometry, const float center_y, const double semitones) noexcept
{
    constexpr double ceiling = common::core::g_bend_ceiling_semitones;
    const TailInterior interior = tailInterior(geometry, center_y);
    const float rest_y = interior.bottom - g_technique_line_thickness / 2.0f;
    const float full_y = interior.top + g_technique_line_thickness / 2.0f;
    const double share = common::core::bendTravel(std::clamp(semitones, 0.0, ceiling)) /
                         common::core::bendTravel(ceiling);
    return rest_y - static_cast<float>(share) * (rest_y - full_y);
}

float bendCurveYAt(
    const TabLaneGeometry& geometry, const float center_y,
    const std::vector<common::core::BendPointViewState>& curve, const double seconds) noexcept
{
    // The first point at or after the instant; the leg into it is the one the instant lies on.
    const auto next = std::ranges::lower_bound(
        curve, seconds, std::ranges::less{}, &common::core::BendPointViewState::seconds);
    if (next == curve.end())
    {
        return bendCurveY(geometry, center_y, curve.empty() ? 0.0 : curve.back().semitones);
    }
    const float next_y = bendCurveY(geometry, center_y, next->semitones);
    if (next == curve.begin() || !(seconds < next->seconds))
    {
        return next_y;
    }
    const auto previous = std::prev(next);
    const float previous_y = bendCurveY(geometry, center_y, previous->semitones);
    const double progress = (seconds - previous->seconds) / (next->seconds - previous->seconds);
    return previous_y + static_cast<float>(progress) * (next_y - previous_y);
}

// Rationale lives on the declaration in tab_lane_layout.h.
bool tailInked(const common::core::NoteViewState& note, const double drawn_end) noexcept
{
    return note.start_seconds < drawn_end;
}

// Rationale lives on the declaration in tab_lane_layout.h.
float cutLegProgress(const float from_x, const float to_x, const float end_x) noexcept
{
    return to_x > from_x ? std::clamp((end_x - from_x) / (to_x - from_x), 0.0f, 1.0f) : 1.0f;
}

// Rationale lives on the declaration in tab_lane_layout.h.
TabBendLeg tabBendLeg(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const std::size_t point, const double drawn_end) noexcept
{
    const float center_y = geometry.laneY(note.string);
    TabBendLeg leg;
    if (point == 0)
    {
        leg.from_x = geometry.x(note.start_seconds);
        leg.from_y = bendCurveY(geometry, center_y, 0.0);
    }
    else
    {
        const common::core::BendPointViewState& before = note.bend[point - 1];
        leg.from_x = geometry.x(before.seconds) + 1.0f;
        leg.from_y = bendCurveY(geometry, center_y, before.semitones);
    }
    const float end_x = geometry.x(drawn_end);
    if (point == note.bend.size())
    {
        leg.to_x = end_x;
        leg.to_y = leg.from_y;
        return leg;
    }
    const common::core::BendPointViewState& into = note.bend[point];
    const float point_x = geometry.x(into.seconds);
    const float point_y = bendCurveY(geometry, center_y, into.semitones);
    leg.cut = !common::core::instantDrawn(into.seconds, drawn_end);
    leg.to_x = leg.cut ? end_x : point_x;
    leg.to_y =
        leg.cut ? leg.from_y + ((point_y - leg.from_y) * cutLegProgress(leg.from_x, point_x, end_x))
                : point_y;
    return leg;
}

// Rationale lives on the declaration in tab_lane_layout.h.
float slideOutChipY(
    const TabLaneGeometry& geometry, const float center_y, const bool upward) noexcept
{
    const TailSpan span = tailSpan(geometry, center_y);
    const float lift = geometry.note_height / 3.0f;
    return upward ? span.top - lift : span.bottom + lift;
}

// Rationale lives on the declaration in tab_lane_layout.h.
std::optional<float> nextHeadLeftEdge(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept
{
    if (!note.end_head.has_value())
    {
        return std::nullopt;
    }
    return geometry.x(note.ring_end_seconds) - geometry.headSize() / 2.0f;
}

} // namespace rock_hero::common::ui
