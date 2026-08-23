/*!
\file highway_floor_geometry.h
\brief The span clamp and floor footprint the highway's floor marks and sustain tails share.

Nothing inside the renderer's draw pass is reachable from a test, so a rule the pass makes more
than once belongs in a small pure unit beside it instead — the reasoning \ref
highway_head_marks.h states at length.

Two such rules live here:

\ref highwayVisibleSpan is the one clamp a drawn span obeys — it starts at the later of the note's
onset and the hit line (the board consumes a ribbon from below as it plays) and stops at the
earlier of the span's own end and the visibility horizon, and there is nothing to draw when those
two cross. Stated once so any two marks over the same note cannot disagree about where its span
begins.

\ref highwayFloorFootprint is where on the fret axis a mark under one note lies, and how wide — a
fretted mark straddling the note's own anchor, an open string's spanning the hand window inset by
the tail margin.
*/

#pragma once

#include "highway/highway_slide_path.h"

#include <algorithm>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <utility>

namespace rock_hero::common::ui
{

/*! \brief One note's drawable span, clamped to the stretch of board that can show it. */
struct HighwaySpan
{
    /*! \brief Where the drawn span starts: the later of the note's onset and the hit line. */
    double from{0.0};

    /*! \brief Where it stops: the earlier of the span's own end and the visibility horizon. */
    double to{0.0};
};

/*!
\brief Clamps one note's span to the stretch of board that can show it.

\param start_seconds The note's onset in absolute seconds.
\param end_seconds The span's end in absolute seconds.
\param now_seconds This frame's song time, which is where the hit line sits.
\param span_end_seconds The visibility horizon in absolute seconds.
\return The clamped span, or nullopt when none of it is on the board — a span that ended before
        this frame, one that never had length, and one still beyond the horizon all report the
        same nothing.
*/
[[nodiscard]] constexpr std::optional<HighwaySpan> highwayVisibleSpan(
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
    return HighwaySpan{.from = from, .to = to};
}

/*!
\brief How far an open string's floor marks stay inside the hand window at each end.

Charter's inset for an open tail, and every floor mark under an open note takes the same one — the
bar the note draws spans the window, so a mark narrower or wider than this would not sit under it.
*/
constexpr double g_open_tail_margin = 0.2;

/*! \brief Where a floor mark under one note lies on the fret axis, and how wide it is. */
struct HighwayFloorFootprint
{
    /*! \brief World X the mark is centred on. */
    double center_x{0.0};

    /*! \brief Half-width around \ref center_x, in world units. */
    double half_width{0.0};
};

/*!
\brief Returns the floor footprint one note's marks occupy, or nothing where none fits.

The two cases a floor mark under a note has, stated once. A FRETTED note's mark straddles the
note's own fretboard anchor at whatever half-width the mark asks for; an OPEN string's spans the
hand window inset by \ref g_open_tail_margin at each end, because its bar does, and the requested
half-width does not apply to it at all.

\param note Projected note the mark is drawn under.
\param fretted_half_width Half-width a fretted mark takes.
\param window_x The hand window's sorted world-X edges AT THE TIME the mark is placed. Read only
       for an open string; a fretted note's footprint does not depend on the hand at all.
\param metrics Board metrics the fret axis is laid out by.
\param mirrored True when the board draws left-handed (world X reflected).
\return The centre and half-width, or nullopt when a tapered neck's window has narrowed past its
        own insets and an open mark has nowhere left to lie.
*/
[[nodiscard]] inline std::optional<HighwayFloorFootprint> highwayFloorFootprint(
    const common::core::NoteViewState& note, double fretted_half_width,
    std::pair<double, double> window_x, const common::core::HighwayMetrics& metrics, bool mirrored)
{
    if (!common::core::openString(note))
    {
        return HighwayFloorFootprint{
            .center_x = highwayNoteFretboardX(note, note.fret, metrics, mirrored),
            .half_width = fretted_half_width,
        };
    }
    const double low = window_x.first + g_open_tail_margin;
    const double high = window_x.second - g_open_tail_margin;
    if (!(high > low))
    {
        return std::nullopt;
    }
    return HighwayFloorFootprint{
        .center_x = (low + high) / 2.0,
        .half_width = (high - low) / 2.0,
    };
}

} // namespace rock_hero::common::ui
