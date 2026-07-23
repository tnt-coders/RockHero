/*!
\file highway_window.h
\brief Continuous hand-window math: eased window edges over time and per-line coverage.
*/

#pragma once

#include <rock_hero/common/core/highway/highway_view_state.h>
#include <vector>

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
    friend constexpr bool operator==(
        const HighwayHandWindow& lhs, const HighwayHandWindow& rhs) noexcept = default;
};

/*!
\brief Returns the eased hand-window extent at an absolute time.

Placements are step values whose approaches ramp: inside a placement's
[seconds - ramp_seconds, seconds] span both edges ease from the previous settled window toward
the arriving one with the pitched slide curve, so the window travels in lockstep with a gliding
note and morphs smoothly for ordinary moves. Outside every ramp the settled window holds, the
reference nut window (lines 0 to 4) applies before the first placement, and arrivals are
inclusive: at exactly \p seconds the placement has arrived.

\param fret_hand_positions Placements in ascending arrival order (HighwayViewState order).
\param seconds Absolute time to evaluate at.
\return Fractional window extent at the time.
*/
[[nodiscard]] HighwayHandWindow highwayHandWindowAt(
    const std::vector<HighwayFhpView>& fret_hand_positions, double seconds) noexcept;

/*!
\brief Returns how deeply the window contains a fret line, as [0, 1] coverage.

One inside the window with at least a whole lane to spare, zero at least a whole lane outside,
ramping linearly across each moving edge. This is the shared signal driving the hit-line
presentation during a transition: lane-border brightness crossfades and fret-number fades both
follow it, so everything at the hit line moves as a single gesture with the sweeping border.

\param window Window extent from highwayHandWindowAt.
\param line Fret-line coordinate to measure (integer lines for the board's fixed lines).
\return Coverage in [0, 1].
*/
[[nodiscard]] double highwayHandWindowLineCoverage(
    const HighwayHandWindow& window, double line) noexcept;

} // namespace rock_hero::common::core
