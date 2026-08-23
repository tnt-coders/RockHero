/*!
\file highway_floor_band.h
\brief The visible-span clamp the highway's sustain tails and its actual-ring floor band share.

Nothing inside the renderer's draw pass is reachable from a test, so a rule the pass makes more
than once belongs in a small pure unit beside it instead — the reasoning \ref
highway_head_marks.h states at length.

This is that shape for the one clamp a drawn span obeys: it starts at the later of the note's
onset and the hit line (the board consumes a ribbon from below as it plays) and stops at the
earlier of the span's own end and the visibility horizon, and there is nothing to draw when those
two cross. The sustain tail and the actual-ring diagnostics band each draw a span of one note
between the same two bounds and differ only in which END they ask about, so stating the clamp
twice would let a diagnostic and the tail directly above it disagree about where the same note's
span begins.
*/

#pragma once

#include <algorithm>
#include <optional>
#include <utility>

namespace rock_hero::common::ui
{

/*!
\brief Clamps one note's span to the stretch of board that can show it.

\param start_seconds The note's onset in absolute seconds.
\param end_seconds The span's end in absolute seconds: the PRESENTED tail's end for a sustain
       ribbon, the ACTUAL ring's end for the diagnostics band.
\param now_seconds This frame's song time, which is where the hit line sits.
\param span_end_seconds The visibility horizon in absolute seconds.
\return The clamped span as [from, to], or nullopt when none of it is on the board — a span that
        ended before this frame, one that never had length, and one still beyond the horizon all
        report the same nothing.
*/
[[nodiscard]] constexpr std::optional<std::pair<double, double>> highwayVisibleSpan(
    double start_seconds, double end_seconds, double now_seconds, double span_end_seconds) noexcept
{
    const double from = std::max(start_seconds, now_seconds);
    const double to = std::min(end_seconds, span_end_seconds);
    // Negated rather than `to <= from` so a NaN bound reports nothing instead of drawing a span
    // no comparison can order.
    if (!(to > from))
    {
        return std::nullopt;
    }
    return std::pair{from, to};
}

} // namespace rock_hero::common::ui
