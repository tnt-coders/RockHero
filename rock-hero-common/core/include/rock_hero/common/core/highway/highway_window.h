/*!
\file highway_window.h
\brief Continuous hand-window math: eased window edges over time and per-line coverage.
*/

#pragma once

#include <compare>
#include <optional>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <span>

namespace rock_hero::common::core
{

/*!
\brief Fractional fret-line extent of the hand window at one instant.

Edges are fret-line coordinates (line 0 is the nut side of fret 1): a settled placement spans
lines fret - 1 through fret + width - 1, and during a transition both edges interpolate
independently, so position moves and width morphs are one mechanism. Fret lines themselves never
move — the window is a region sliding over the fixed board.
*/
struct HighwayHandWindow
{
    /*! \brief Fret-line coordinate of the window's low-fret edge. */
    double low_line{0.0};

    /*! \brief Fret-line coordinate of the window's high-fret edge. */
    double high_line{4.0};

    /*!
    \brief Compares two window extents by their stored fields.
    \param lhs Left-hand window.
    \param rhs Right-hand window.
    \return True when both windows store equal values.
    */
    // Window-edge equality is intentionally exact: settled placements land on exact fret-line
    // coordinates, and callers comparing a morphing window compare with a tolerance instead.
    // This is not defaulted because the generated comparison uses direct floating-point ==,
    // which is promoted to a build error by -Wfloat-equal under the shared warning policy.
    // std::is_eq(lhs.low_line <=> rhs.low_line) preserves exact equality semantics while
    // avoiding that compiler diagnostic.
    friend constexpr bool operator==(
        const HighwayHandWindow& lhs, const HighwayHandWindow& rhs) noexcept
    {
        return std::is_eq(lhs.low_line <=> rhs.low_line) &&
               std::is_eq(lhs.high_line <=> rhs.high_line);
    }
};

/*!
\brief Returns the eased extent of a hand's window at an absolute time.

THE ONE MORPH both hands move by. A track is arrivals in ascending order
(\ref HighwayHandArrival): the fretting hand's placements, or the picking hand's strikes and their
travel. Inside an arrival's [seconds - ramp_seconds, seconds] span both edges ease from the previous
settled window toward the arriving one, so the window travels in lockstep with a gliding note and
morphs smoothly for ordinary moves. Which easing applies is the arrival's own `unpitched_ramp`: a
pitched approach takes the slide curve, an unpitched one the slide-out curve, which starts slowly
because a hand letting go does not accelerate the way one arriving does. An arrival's
`settle_seconds` is the crop zone: over that final stretch the approach leaves its curve — where
the rail it follows is cut — and comes to rest at the arrival with a continuous slope, in place of
the unpitched curve's stop with slope. Outside every ramp the settled window holds, and arrivals
are inclusive: at exactly \p seconds the arrival has arrived. The first arrival's settled window
already holds from the start of time — the opening scroll shows where the hand belongs before the
first note arrives — and the reference nut window (lines 0 to 4) applies only when there are no
arrivals at all.

\param track Arrivals in ascending order.
\param seconds Absolute time to evaluate at.
\return Fractional window extent at the time.
*/
[[nodiscard]] HighwayHandWindow highwayHandWindowAt(
    std::span<const HighwayHandArrival> track, double seconds) noexcept;

/*!
\brief The leg a hand's window is travelling at one instant: the ramp from one arrival's settled
window into the next's.
*/
struct HighwayHandLeg
{
    /*! \brief The arrival the leg leaves; never null. */
    const HighwayHandArrival* from{nullptr};

    /*! \brief The arrival the leg ramps into; never null. */
    const HighwayHandArrival* to{nullptr};

    /*! \brief How far through the ramp the instant lies, in [0, 1). */
    double progress{0.0};
};

/*!
\brief Returns the leg whose ramp is in progress at an absolute time, if any.

THE LEG LOOKUP every reader of motion asks — the window's easing (\ref highwayHandWindowAt), a
light's reading after its release (\ref highwayLitTrackTime), and the renderer's motion dim. The
leg into an arrival `to` is in progress from `to.seconds - to.ramp_seconds` inclusive to
`to.seconds` exclusive (at the arrival it has arrived). A zero ramp has no leg, and neither does the
first arrival: its window already holds before it arrives.

The leg points into \p track, so it is valid only while the track is.

\param track Arrivals in ascending order.
\param seconds Absolute time to evaluate at.
\return The leg in progress, or no value where the window holds still.
*/
[[nodiscard]] std::optional<HighwayHandLeg> highwayHandLegAt(
    std::span<const HighwayHandArrival> track, double seconds) noexcept;

/*!
\brief Returns the instant a lit light reads its hand's track at: where the window it shows is
taken from (\ref highwayLitWindowAt).

THE READING RULE, stated once: a light never starts a new leg of its track outside its own stretch,
because a placement whose ramp lies in a dark gap must not move a light nothing displays — neither
the fading light before the gap nor the rising one after it. So:

- inside the stretch, [start, release], the light reads the track at \p seconds;
- during the rise, before the start, it reads the track at the start: the placement is already in
  place as the light rises, with no morph;
- after the release it follows only the leg already in progress AT the release — the arrival whose
  ramp has begun by then and not yet arrived — to that leg's end, and holds there; with no leg in
  progress it holds the release's window. A slide-out's settle over its crop zone therefore still
  plays out, while a leg that begins inside the decay never moves the light.

The result never decreases as \p seconds grows, and the light's window moves only between its
readings at two instants, which is what lets a drawer bound its samples.

\param track Arrivals in ascending order.
\param stretch The light's lit stretch over that track.
\param seconds Absolute time the light is drawn at.
\return The absolute time the track is read at.
*/
[[nodiscard]] double highwayLitTrackTime(
    std::span<const HighwayHandArrival> track, const HighwayLitStretch& stretch,
    double seconds) noexcept;

/*!
\brief Returns the extent a lit light shows at an absolute time: its hand's window read at
\ref highwayLitTrackTime.

Every layer that draws a light — the floor, the lane-border tiers, the fret-line tier — takes the
light's extent from here, never from \ref highwayHandWindowAt directly, so no layer can show a move
the light itself does not make.

\param track Arrivals in ascending order.
\param stretch The light's lit stretch over that track.
\param seconds Absolute time the light is drawn at.
\return Fractional window extent the light covers at the time.
*/
[[nodiscard]] HighwayHandWindow highwayLitWindowAt(
    std::span<const HighwayHandArrival> track, const HighwayLitStretch& stretch,
    double seconds) noexcept;

/*!
\brief Returns how deeply the window contains a fret line, as [0, 1] coverage.

Exactly one ON the window's own edge lines and anywhere inside, zero a whole lane outside, ramping
linearly over the lane OUTSIDE each edge — from one line below the low edge up to that edge, and
symmetrically above the high one. The ramp lies wholly outside the window, so a line needs no spare
lane to score one; reimplementing this from a "lane to spare inside" reading offsets the whole
crossfade by a full lane. This is the shared signal driving the hit-line
presentation during a transition: lane-border brightness crossfades and fret-number fades both
follow it, so everything at the hit line moves as a single gesture with the sweeping border.

\param window Window extent from highwayHandWindowAt.
\param line Fret-line coordinate to measure (integer lines for the board's fixed lines).
\return Coverage in [0, 1].
*/
[[nodiscard]] double highwayHandWindowLineCoverage(
    const HighwayHandWindow& window, double line) noexcept;

} // namespace rock_hero::common::core
