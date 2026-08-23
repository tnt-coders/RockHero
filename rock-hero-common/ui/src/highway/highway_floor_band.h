/*!
\file highway_floor_band.h
\brief The span clamp, floor footprint, and per-note gate the highway's floor marks share.

Nothing inside the renderer's draw pass is reachable from a test, so a rule the pass makes more
than once belongs in a small pure unit beside it instead — the reasoning \ref
highway_head_marks.h states at length.

Three such rules live here, each read by a sustain tail and by an actual-ring diagnostics mark,
plus the one envelope the light form runs:

\ref highwayVisibleSpan is the one clamp a drawn span obeys — it starts at the later of the note's
onset and the hit line (the board consumes a ribbon from below as it plays) and stops at the
earlier of the span's own end and the visibility horizon, and there is nothing to draw when those
two cross. The tail and the diagnostics mark differ only in which END they ask about, so stating
the clamp twice would let a diagnostic and the tail directly above it disagree about where the same
note's span begins. It also reports whether the far end was clamped, because "did this span
genuinely end on the board" is the same question a cap and a fade-out both ask.

\ref highwayFloorLightEnvelope is the soft-ended alpha a floor light runs over that span, with
\ref highwayFloorLightRamp the ramp its ends take — clamped so the two can never cross, which is the
one arithmetic fact that decides whether the shortest rings draw at all.

\ref highwayFloorFootprint is where on the fret axis a floor mark under one note lies, and how wide
— a fretted mark straddling the note's own anchor, an open string's spanning the hand window inset
by the tail margin. The tail's band stations, the actual-ring band and the actual-ring light all
take that pair and differ only in the width they ask for.

\ref highwayRingMarkApplies is which notes an actual-ring mark is drawn for at all, so the two
filters the diagnostics options carry are answered once for every form the mark takes.
*/

#pragma once

#include "highway/highway_slide_path.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/ui/highway/highway_diagnostics_options.h>
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

    /*!
    \brief True when the span's own end is what stopped it, rather than the horizon.

    The fact a mark needs before claiming an END: a cap or a fade-out at \ref to is honest only
    when the span genuinely finishes there, and one drawn where the horizon cut it off would state
    an ending the chart does not have.
    */
    bool ends_inside{false};
};

/*!
\brief Clamps one note's span to the stretch of board that can show it.

\param start_seconds The note's onset in absolute seconds.
\param end_seconds The span's end in absolute seconds: the PRESENTED tail's end for a sustain
       ribbon, the ACTUAL ring's end for a diagnostics mark.
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
    return HighwaySpan{.from = from, .to = to, .ends_inside = end_seconds <= span_end_seconds};
}

/*!
\brief The ramp a floor light's soft ends actually run over, for a span of a given length.

The asked-for ramp, clamped to half the span: on a span shorter than two ramps the rise and the
fall would otherwise CROSS, and their crossing — the envelope's real peak — is a corner that no
exact time the light draws at lands on, so a span of one ramp or less came out at alpha zero at
both of its only two times and drew nothing at all (a dead note's ring, a chug's — the notes the
rig exists to show). Clamped, the two corners meet at the midpoint at worst and never cross.

\param start_seconds The span's start in absolute seconds.
\param end_seconds The span's end in absolute seconds; must be later than `start_seconds`.
\param ramp_seconds The ramp asked for, in seconds.
\return The ramp to run each soft end over, positive whenever the span has length.
*/
[[nodiscard]] constexpr double highwayFloorLightRamp(
    double start_seconds, double end_seconds, double ramp_seconds) noexcept
{
    return std::min(ramp_seconds, (end_seconds - start_seconds) / 2.0);
}

/*!
\brief A floor light's alpha envelope at a time, as a fraction of its peak.

Rises from nothing at the span's start and falls back to nothing at its end, each over
\ref highwayFloorLightRamp, and holds the peak between. The fall applies only where the span's end
is its OWN (\ref HighwaySpan::ends_inside): a fade at the horizon would claim an ending the chart
does not have.

\param start_seconds The span's start in absolute seconds.
\param end_seconds The span's end in absolute seconds; must be later than `start_seconds`.
\param ends_inside True when the span genuinely ends at `end_seconds` rather than at the horizon.
\param ramp_seconds The ramp asked for, in seconds, before the clamp.
\param seconds Absolute time to evaluate at.
\return The envelope in [0, 1].
*/
[[nodiscard]] constexpr double highwayFloorLightEnvelope(
    double start_seconds, double end_seconds, bool ends_inside, double ramp_seconds,
    double seconds) noexcept
{
    const double ramp = highwayFloorLightRamp(start_seconds, end_seconds, ramp_seconds);
    const double rise = (seconds - start_seconds) / ramp;
    const double fall = ends_inside ? (end_seconds - seconds) / ramp : 1.0;
    return std::clamp(std::min(rise, fall), 0.0, 1.0);
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
\param fretted_half_width Half-width a fretted mark takes: the tail's own for a ribbon, a multiple
       of it for a diagnostics mark that must show a rim beside one.
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

/*!
\brief Whether one note gets an actual-ring diagnostics mark at all.

The two filters ride the whole rig rather than one of its forms: they say WHICH notes are marked,
which the light, the fill and the outline all have to answer the same way, so the pass asks this
once per note and then chooses a form.

Both filters are stated positively, so the defaults read as "mark everything" and a reader never
has to invert a name to know what the rig starts as.

\param fretting_hand_count Fretting-hand members of the note's onset group — the chord box's own
       count, so the "already stated by the box" filter uses the box's own rule
       (\ref common::core::highwayChordBoxApplies).
\param has_presented_tail True when the note draws a tail of its own (its presented end is later
       than its onset), which is the CHART fact and not "a tail is visible this frame" — the
       latter flickers as a tail scrolls off the horizon.
\param options The diagnostics switches this frame draws with.
\return True when the note gets a mark.
*/
[[nodiscard]] constexpr bool highwayRingMarkApplies(
    std::size_t fretting_hand_count, bool has_presented_tail,
    HighwayDiagnosticsOptions options) noexcept
{
    if (options.actual_ring == ActualRingLook::Off)
    {
        return false;
    }
    if (!options.actual_ring_marks_tailed_notes && has_presented_tail)
    {
        return false;
    }
    return options.actual_ring_marks_chord_members ||
           !common::core::highwayChordBoxApplies(fretting_hand_count);
}

} // namespace rock_hero::common::ui
