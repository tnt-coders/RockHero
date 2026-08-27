/*!
\file chart_hit_testing.h
\brief Headless hit resolution mapping tablature-lane pixels to chart objects.

Every rectangle comes from the shared layout manifest, computed from the same TabLaneGeometry the
paint core drew with and from the same note stream its tail pass draws from, so hit policy can
never drift from the rendered pixels: every drawn ribbon is clickable and nothing undrawn is.
*/

#pragma once

#include "chart/chart_selection.h"

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief A note the pointer resolved, by index into the projection's note order. */
struct ChartNoteHit
{
    /*! \brief Index into \ref common::core::ChartViewState::notes. */
    std::size_t index{0};

    /*!
    \brief Compares two note hits by their stored values.
    \param lhs Left-hand hit.
    \param rhs Right-hand hit.
    \return True when both name the same note.
    */
    friend constexpr bool operator==(const ChartNoteHit& lhs, const ChartNoteHit& rhs) noexcept =
        default;
};

/*!
\brief A waypoint the pointer resolved: which projected note, and which of its drawn waypoints.

Two indices rather than one, which is why the hit target is a sum: a waypoint belongs to a note,
so no single index into a flat array names it.
*/
struct ChartWaypointHit
{
    /*! \brief Index into \ref common::core::ChartViewState::notes. */
    std::size_t note_index{0};

    /*! \brief Index into that note's \ref common::core::NoteViewState::slides. */
    std::size_t waypoint_index{0};

    /*!
    \brief Compares two waypoint hits by their stored values.
    \param lhs Left-hand hit.
    \param rhs Right-hand hit.
    \return True when both name the same waypoint.
    */
    friend constexpr bool operator==(
        const ChartWaypointHit& lhs, const ChartWaypointHit& rhs) noexcept = default;
};

/*!
\brief One selectable object the lane resolved under a pointer.

The same two alternatives \ref ChartSelectionKey has, addressed by projection index instead of by
identity: the controller turns one into the other, which is the single place a drawn glyph becomes
a selectable object.
*/
using ChartHitTarget = std::variant<ChartNoteHit, ChartWaypointHit>;

/*!
\brief Resolves the selectable object under a lane-local point, if any.

Topmost drawn wins, which is the rule and the reason for the order below — with ONE stated
exception. Silently-held stops resolve FIRST even though the paint core draws their brackets UNDER
the heads: a hold's bracket is its only affordance and never wraps a head of its own string (that
string is silent at the span start by construction), so all the priority takes is the near columns
of a head a little later on that string, which the head can spare and a two-pixel bracket bar
cannot. The exception is recorded with the verb's design record rather than left to be inferred
from this order. Then note heads, which win over sustain tails (a
head sitting on another note's tail takes the click), nearest onset center first among overlapping
heads. Then the linked waypoint heads riding a tail, which are drawn ON the ribbon and so must win
over it. Then tails, resolving to the note whose tail rectangle contains the point, nearest onset
first.

Two objects the lane draws nothing for are not hit-testable, because nothing undrawn is. A waypoint
carries a head only when it is LINKED (\ref common::core::linkedWaypoint), and one stating no fret
draws nothing at all today — how those should draw, and therefore how a pointer should reach them,
is the bend display study's question and not this function's. A silent hold whose stop joined no
posture draws no bracket, which \ref common::ui::tabSilentHoldLayout answers with no layout at all,
so the skip needs no rule of its own here.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param x Pointer x in lane-local pixels.
\param y Pointer y in lane-local pixels.
\return The hit object, or empty for an empty-lane point.
*/
[[nodiscard]] std::optional<ChartHitTarget> chartHitTarget(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y);

/*!
\brief Collects the objects whose head or mark rectangles intersect a marquee box.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param left Left edge of the box in lane-local pixels.
\param top Top edge of the box in lane-local pixels.
\param right Right edge of the box in lane-local pixels.
\param bottom Bottom edge of the box in lane-local pixels.
\return Boxed objects: heads first, then silent holds, then waypoints, each in projection order.
*/
[[nodiscard]] std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom);

} // namespace rock_hero::editor::core
