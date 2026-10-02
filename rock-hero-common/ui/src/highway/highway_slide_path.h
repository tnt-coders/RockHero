/*!
\file highway_slide_path.h
\brief Where a stopped note sits on the board's fret axis, at its onset and anywhere along a glide.

Nothing inside the renderer's draw pass is reachable from a test, so a rule the pass makes more
than once belongs in a small pure unit beside it instead — the reasoning \ref highway_head_marks.h
states at length, and \ref highway_floor_geometry.h repeats one level smaller.

This is that shape for the fret axis. Three consumers ask the glide where a note is at a time — the
head's anchor, the tail's arc probe and the tail's per-sample walk — and the anchor underneath it is
asked by those plus the fret-span furniture. Both are pure functions of a projected note, the board
metrics and a time, so out here every pass of `draw()` — the floor passes included — can ask them,
and they gain the witness the draw pass can never have.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_view_state.h>

namespace rock_hero::common::ui
{

/*!
\brief Alpha an unpitched (pressure-release) glide has dimmed to by the end of its run.

The slide-out is a fading gesture rather than a sounding stop, so the rail dims toward this across
the release — a fretted note's slide-out leg, a scrape's whole path — instead of holding the note's
own brightness to the last keyframe.
*/
constexpr double g_unpitched_slide_end_alpha = 0.25;

/*!
\brief Where a STOPPED note sounds on the fretboard axis, and the one authority for that anchor.

A genuine open string never asks it — the open-string bar across the hand window is its own
treatment. That branch is gated on \ref common::core::openString, which a node makes false, so a
natural harmonic's fret 0 does reach here, and lands on its node exactly as any other harmonic
does.

A harmonic is touched AT its node rather than behind a fret wire, so its head, its tail and every
point its glide passes through must all read the same value: the node lands just past a wire while
the fret slot's middle sits between the two wires behind it, half a slot away.

The stop is a parameter because a gesture sounds from more than one of them — the onset from the
note's own fret, a slide from each fret it travels to — and a harmonic's node RIDES its stop: fret
spacing is logarithmic, so the node's offset above the stop is constant in fret units and a glide
that moves the stop moves the node by the same amount. Passing `note.fret` therefore gives the
onset's anchor, and the shift is zero there. This is the same rule `tabNoteHeadText` labels every
head of a gesture by, so the two surfaces cannot disagree about what a glide arrives at.

Note the asymmetry with the fret-span line, which marks where the HAND goes: on an artificial
harmonic the hand presses at `fret` while the sound comes from the node twelve-or-so frets up, and
both facts are drawn, because that line runs from the stop's fret slot to the node rather than
sitting on either one alone. A note's own glow post is not furniture — it is the head's shadow, so
it travels with the head, node shift and glide included. A KEYFRAME's post is furniture and does
stay on its stop, because that is a place the hand goes.

A pinch harmonic's node belongs to the PICKING hand, so the fretting hand stays on the stop and
this returns the ordinary fret slot; that node still waits for its own right-hand cue.

\param note Projected note whose gesture is being placed.
\param fret_at_point Stop being placed — the note's own fret, or a slide keyframe's target.
\param metrics Board metrics the fret axis is laid out by.
\param mirrored True when the board draws left-handed (world X reflected).
\return World X of the stop, with a harmonic's node shift applied.
*/
[[nodiscard]] inline double highwayNoteFretboardX(
    const common::core::NoteViewState& note, int fret_at_point,
    const common::core::HighwayMetrics& metrics, bool mirrored)
{
    // The one placement authority (\ref highwayStopX): a node on its own wire, a fret at its
    // slot's midpoint.
    return common::core::highwayStopX(
        common::core::highwayDrawnStop(note, fret_at_point), metrics, mirrored);
}

/*! \brief Where a note's glide has travelled to at an instant, and how far it has faded. */
struct HighwaySlideState
{
    /*! \brief World-X offset from the note's own onset anchor; zero before it moves. */
    double x_offset{0.0};

    /*!
    \brief Brightness across a release, toward \ref g_unpitched_slide_end_alpha; 1.0 elsewhere.
    */
    double alpha{1.0};
};

/*!
\brief Returns a note's glide state at a time: the eased X offset from its anchor, and the dim.

The ONE authority for a note's lateral travel. Segments run between keyframe anchors, eased by
\ref common::core::highwaySlideEaseWeight in the family the ARRIVING keyframe names (a pitched
glide accelerates into its target; an unpitched one releases early), and past the last keyframe the
glide holds its target, since a gesture that has stopped travelling continues straight along the
fret it stopped on.

Every stop sits at its stored instant, including stops past the note's ink end, and the walk reads
all of them: a time inside the drawn extent on the leg toward a stop beyond it lies on that leg's
true path. What is DRAWN is the caller's to bound — it evaluates only times within its extent,
so a stop past the ink end moves nothing on the board.

The dim spans a scrape's WHOLE path rather than one segment: a scrape's chained legs are one
continuous release, so the alpha must never snap back to full at a direction reversal — only the
geometry restarts per leg, each interior leg on the pitched curve and the terminal on the release
curve. On any other note the dim is the terminal slide-out's own leg.

\param note Projected note whose glide is being read.
\param base_x World X the offset is measured from — the note's own anchor for a fretted gesture.
\param metrics Board metrics the fret axis is laid out by.
\param mirrored True when the board draws left-handed (world X reflected).
\param seconds Absolute position to evaluate at.
\return The offset from `base_x` and the slide-out dim; a still note reports zero and 1.0.
*/
[[nodiscard]] inline HighwaySlideState highwaySlideStateAt(
    const common::core::NoteViewState& note, double base_x,
    const common::core::HighwayMetrics& metrics, bool mirrored, double seconds)
{
    // The gesture read as one uniform sequence — the note's position keyframes, then its
    // slide-out terminal — through the shared stop accessor. Every stop, not only those within
    // the drawn extent: the leg an extent cuts runs toward the first stop beyond it, and a
    // release dims across its whole stored length.
    const std::size_t stop_count = note.slides.size();
    if (stop_count == 0 || note.fret <= 0)
    {
        return HighwaySlideState{.x_offset = 0.0, .alpha = 1.0};
    }
    // THE DIM, stated once for the legs and for the hold past the last stop: a scrape's whole
    // path is one release, a fretted note's slide-out leg alone is one, and every other leg holds
    // full brightness.
    const bool scrape = common::core::isScrape(note.attack);
    const auto alpha_at = [&note, stop_count, scrape](const std::size_t segment, const double at) {
        if (!scrape && !note.slides[segment].slide_out)
        {
            return 1.0;
        }
        const std::size_t run_begin = scrape ? 0 : segment;
        const std::size_t run_end = scrape ? stop_count - 1 : segment;
        const double run_start_seconds =
            run_begin == 0 ? note.start_seconds : note.slides[run_begin - 1].seconds;
        const double run_span = note.slides[run_end].seconds - run_start_seconds;
        const double progress =
            run_span > 0.0 ? std::clamp((at - run_start_seconds) / run_span, 0.0, 1.0) : 1.0;
        return 1.0 + ((g_unpitched_slide_end_alpha - 1.0) * progress);
    };
    double segment_start_seconds = note.start_seconds;
    double segment_start_x = base_x;
    for (std::size_t index = 0; index < stop_count; ++index)
    {
        const common::core::SlideStopViewState& stop = note.slides[index];
        const double stop_x = highwayNoteFretboardX(note, stop.fret, metrics, mirrored);
        if (seconds <= stop.seconds)
        {
            const double span = stop.seconds - segment_start_seconds;
            const double progress =
                span > 0.0 ? std::clamp((seconds - segment_start_seconds) / span, 0.0, 1.0) : 1.0;
            const double weight = common::core::highwaySlideEaseWeight(progress, stop.slide_out);
            return HighwaySlideState{
                .x_offset = segment_start_x + ((stop_x - segment_start_x) * weight) - base_x,
                .alpha = alpha_at(index, seconds),
            };
        }
        segment_start_seconds = stop.seconds;
        segment_start_x = stop_x;
    }
    // Past the last stop the glide holds its target, and the dim holds where its run ended.
    const common::core::SlideStopViewState& last = note.slides[stop_count - 1];
    return HighwaySlideState{
        .x_offset = highwayNoteFretboardX(note, last.fret, metrics, mirrored) - base_x,
        .alpha = alpha_at(stop_count - 1, seconds),
    };
}

} // namespace rock_hero::common::ui
