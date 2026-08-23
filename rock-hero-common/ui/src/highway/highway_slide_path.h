/*!
\file highway_slide_path.h
\brief Where a stopped note sits on the board's fret axis, at its onset and anywhere along a glide.

Nothing inside the renderer's draw pass is reachable from a test, so a rule the pass makes more
than once belongs in a small pure unit beside it instead — the reasoning \ref highway_head_marks.h
states at length, and \ref highway_floor_band.h repeats one level smaller.

This is that shape for the fret axis. Four consumers already asked the glide where a note is at a
time — the head's anchor, the tail's arc probe, the tail's per-sample walk, and now the actual-ring
floor light — and the anchor underneath it is asked by those plus the fret-span furniture. Both
were written inline in `draw()`, the glide as a sixty-line lambda declared AFTER every floor pass,
which is what made a floor mark unable to follow a slide at all without moving it. They are pure
functions of a projected note, the board metrics and a time, so out here they gain the witness the
draw pass can never have.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_view_state.h>

namespace rock_hero::common::ui
{

/*!
\brief Alpha an unpitched (pressure-release) glide has dimmed to by the end of its run.

The release is a fading gesture rather than a sounding stop, so the rail dims toward this across
the whole consecutive unpitched run instead of holding the note's own brightness to the last
waypoint.
*/
constexpr double g_unpitched_slide_end_alpha = 0.25;

/*!
\brief Where a STOPPED note sounds on the fretboard axis, and the one authority for that anchor.

A fret-0 note never asks it — the open-string bar across the hand window is its own treatment.

A stopped harmonic (a tapped artificial, say — a natural's fret 0 takes the open-string bar and
never reaches here) is touched AT its node rather than behind a fret wire, so its head, its tail
and every point its glide passes through must all read the same value: the node lands just past a
wire while the fret slot's middle sits between the two wires behind it, half a slot away.

The stop is a parameter because a gesture sounds from more than one of them — the onset from the
note's own fret, a slide from each fret it travels to — and a harmonic's node RIDES its stop: fret
spacing is logarithmic, so the node's offset above the stop is constant in fret units and a glide
that moves the stop moves the node by the same amount. Passing `note.fret` therefore gives the
onset's anchor, and the shift is zero there. This is the same rule `tabNoteHeadText` labels every
head of a gesture by, so the two surfaces cannot disagree about what a glide arrives at.

Note the asymmetry with the fret-span line, which marks where the HAND goes and so stays on the
stop: on an artificial harmonic the hand presses at `fret` while the sound comes from the node
twelve-or-so frets up, and both facts are drawn. A note's own glow post is not furniture — it is
the head's shadow, so it travels with the head, node shift and glide included. A WAYPOINT's post is
furniture and does stay on its stop, because that is a place the hand goes.

A pinch harmonic's node belongs to the PICKING hand, so the fretting hand stays on the stop and
this returns the ordinary fret slot; that node still waits for its own right-hand cue (25-Q5).

\param note Projected note whose gesture is being placed.
\param fret_at_point Stop being placed — the note's own fret, or a slide waypoint's target.
\param metrics Board metrics the fret axis is laid out by.
\param mirrored True when the board draws left-handed (world X reflected).
\return World X of the stop, with a harmonic's node shift applied.
*/
[[nodiscard]] inline double highwayNoteFretboardX(
    const common::core::NoteViewState& note, int fret_at_point,
    const common::core::HighwayMetrics& metrics, bool mirrored)
{
    const common::core::SoundingPosition sounding =
        common::core::highwayDrawnSoundingPosition(note, fret_at_point);
    if (sounding.at_node)
    {
        // The fret axis takes a fractional coordinate directly, so the node needs no rounding of
        // any kind here. This is NOT the ceil that `fretFor` applies: that one asks which integer
        // fret *contains* the node, for the hand window; this wants the node's exact position.
        return common::core::highwayFretLineX(sounding.position, metrics, mirrored);
    }
    return common::core::highwayNoteCenterX(fret_at_point, metrics, mirrored);
}

/*! \brief Where a note's glide has travelled to at an instant, and how far it has faded. */
struct HighwaySlideState
{
    /*! \brief World-X offset from the note's own onset anchor; zero before it moves. */
    double x_offset{0.0};

    /*! \brief Brightness scale from the unpitched release; 1.0 for a pitched glide. */
    double alpha{1.0};
};

/*!
\brief Returns a note's glide state at a time: the eased X offset from its anchor, and the dim.

The ONE authority for a note's lateral travel. Segments run between waypoint anchors, eased by
\ref common::core::highwaySlideEaseWeight in the family the ARRIVING waypoint names (a pitched
glide accelerates into its target; an unpitched one releases early), and past the last waypoint the
glide holds its target — which is also what a mark drawn past the presented tail wants, since a
gesture that has stopped travelling continues straight along the fret it stopped on.

One caveat rides that hold, and only a mark drawn past the PRESENTED end can see it: presentation
compresses an unpitched slide-out's terminal earlier than the stored gesture, so for those notes the
held position past the presented end is the trail-off's compressed end fret while the pick was, in
the stored form, still travelling. The board carries the actual ring's LENGTH and no actual-form
gesture geometry, so nothing here can do better; a second producer for the untrimmed path would
state the glide twice.

The dim spans the whole CONSECUTIVE unpitched run rather than one segment: a scrape's chained legs
are one continuous release, so the alpha must never snap back to full at a direction reversal —
only the geometry restarts per leg. A lone terminal slide-out is a one-segment run, which reduces
to the original per-segment dim.

\param note Projected note whose glide is being read.
\param base_x World X the offset is measured from — the note's own anchor for a fretted gesture.
\param metrics Board metrics the fret axis is laid out by.
\param mirrored True when the board draws left-handed (world X reflected).
\param seconds Absolute position to evaluate at.
\return The offset from `base_x` and the release dim; a still note reports zero and 1.0.
*/
[[nodiscard]] inline HighwaySlideState highwaySlideStateAt(
    const common::core::NoteViewState& note, double base_x,
    const common::core::HighwayMetrics& metrics, bool mirrored, double seconds)
{
    if (note.slides.empty() || note.fret <= 0)
    {
        return HighwaySlideState{.x_offset = 0.0, .alpha = 1.0};
    }
    const auto unpitched_alpha_at = [&note](const std::size_t segment, const double at) {
        std::size_t run_begin = segment;
        while (run_begin > 0 && note.slides[run_begin - 1].unpitched)
        {
            --run_begin;
        }
        std::size_t run_end = segment;
        while (run_end + 1 < note.slides.size() && note.slides[run_end + 1].unpitched)
        {
            ++run_end;
        }
        const double run_start_seconds =
            run_begin == 0 ? note.start_seconds : note.slides[run_begin - 1].seconds;
        const double run_span = note.slides[run_end].seconds - run_start_seconds;
        const double progress =
            run_span > 0.0 ? std::clamp((at - run_start_seconds) / run_span, 0.0, 1.0) : 1.0;
        return 1.0 + ((g_unpitched_slide_end_alpha - 1.0) * progress);
    };
    double segment_start_seconds = note.start_seconds;
    double segment_start_x = base_x;
    for (std::size_t index = 0; index < note.slides.size(); ++index)
    {
        const common::core::SlideViewState& waypoint = note.slides[index];
        const double waypoint_x = highwayNoteFretboardX(note, waypoint.fret, metrics, mirrored);
        if (seconds <= waypoint.seconds)
        {
            const double span = waypoint.seconds - segment_start_seconds;
            const double progress =
                span > 0.0 ? std::clamp((seconds - segment_start_seconds) / span, 0.0, 1.0) : 1.0;
            const double weight =
                common::core::highwaySlideEaseWeight(progress, waypoint.unpitched);
            const double alpha = waypoint.unpitched ? unpitched_alpha_at(index, seconds) : 1.0;
            return HighwaySlideState{
                .x_offset = segment_start_x + ((waypoint_x - segment_start_x) * weight) - base_x,
                .alpha = alpha,
            };
        }
        segment_start_seconds = waypoint.seconds;
        segment_start_x = waypoint_x;
    }
    // Past the last waypoint the glide holds its target (and any unpitched dimming).
    const common::core::SlideViewState& last = note.slides.back();
    return HighwaySlideState{
        .x_offset = highwayNoteFretboardX(note, last.fret, metrics, mirrored) - base_x,
        .alpha = last.unpitched ? g_unpitched_slide_end_alpha : 1.0,
    };
}

/*!
\brief Slices per fret of travel a gliding segment is subdivided into.

Six slices flat sufficed for a tapped glide's few-fret travel but faceted a scrape's dozen-fret leg
into visible straights. Four per fret keeps the eased curve under half a fret per slice at its
steepest, which is what makes the density a property of the TRAVEL rather than of the segment.
*/
constexpr double g_glide_slices_per_fret = 4.0;

/*! \brief Floor on the slice count, so a travel of well under a fret still reads as a curve. */
constexpr int g_glide_slice_min = 6;

/*! \brief Ceiling on the slice count, so a full-neck sweep stays inside a batch's budget. */
constexpr int g_glide_slice_max = 64;

/*!
\brief How many straight slices a gliding segment is drawn as, for a travel in fret units.

The one density policy every glide-following mark obeys: the tapping hand's light patches and the
actual-ring floor light both subdivide an eased segment by it, so a scrape cannot facet under one
mark while staying smooth under the other.

\param sweep_frets Absolute travel across the segment in fret units; a finite, non-negative value.
\return Slice count, never below \ref g_glide_slice_min nor above \ref g_glide_slice_max.
*/
[[nodiscard]] inline int highwayGlideSliceCount(double sweep_frets)
{
    return std::clamp(
        static_cast<int>(std::ceil(sweep_frets * g_glide_slices_per_fret)),
        g_glide_slice_min,
        g_glide_slice_max);
}

} // namespace rock_hero::common::ui
