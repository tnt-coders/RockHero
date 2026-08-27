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

/*! \brief A hold marker the pointer resolved, by index into the projection's marker order. */
struct ChartHoldMarkerHit
{
    /*! \brief Index into \ref common::core::ChartViewState::hold_markers. */
    std::size_t index{0};

    /*!
    \brief Compares two marker hits by their stored values.
    \param lhs Left-hand hit.
    \param rhs Right-hand hit.
    \return True when both name the same marker.
    */
    friend constexpr bool operator==(
        const ChartHoldMarkerHit& lhs, const ChartHoldMarkerHit& rhs) noexcept = default;
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

The same three alternatives \ref ChartSelectionKey has, addressed by projection index instead of by
identity: the controller turns one into the other, which is the single place a drawn glyph becomes
a selectable object.
*/
using ChartHitTarget = std::variant<ChartNoteHit, ChartHoldMarkerHit, ChartWaypointHit>;

/*!
\brief Resolves the selectable object under a lane-local point, if any.

Topmost drawn wins, which is the one rule and the reason for the order below. Hold-marker marks
resolve FIRST because the editor draws them as an overlay above the notation, so what a pointer
sits on top of is what it takes. Then note heads, which win over sustain tails (a head sitting on
another note's tail takes the click), nearest onset center first among overlapping heads. Then the
linked waypoint heads riding a tail, which are drawn ON the ribbon and so must win over it. Then
tails, resolving to the note whose tail rectangle contains the point, nearest onset first.

A waypoint the lane draws no head for is not hit-testable, because nothing undrawn is: only a
LINKED waypoint carries a head (\ref common::core::linkedWaypoint), and a waypoint stating no fret
draws nothing at all today — how those should draw, and therefore how a pointer should reach them,
is the bend display study's question and not this function's.

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
\return Boxed objects: notes first, then hold markers, then waypoints, each in projection order.
*/
[[nodiscard]] std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom);

} // namespace rock_hero::editor::core
