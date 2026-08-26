/*!
\file chart_hit_testing.h
\brief Headless hit resolution mapping tablature-lane pixels to chart notes.

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
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One selectable object the lane resolved under a pointer. */
struct ChartHitTarget
{
    /*! \brief Which authored array the object lives in. */
    ChartSelectableKind kind{ChartSelectableKind::Note};

    /*! \brief Index into that array's projected list, in the projection's own order. */
    std::size_t index{0};

    /*!
    \brief Compares two hit targets by their stored values.
    \param lhs Left-hand target.
    \param rhs Right-hand target.
    \return True when both targets store equal values.
    */
    friend constexpr bool operator==(
        const ChartHitTarget& lhs, const ChartHitTarget& rhs) noexcept = default;
};

/*!
\brief Resolves the selectable object under a lane-local point, if any.

Topmost drawn wins, which is the one rule and the reason for the order below. Hold-marker marks
resolve FIRST because the editor draws them as an overlay above the notation, so what a pointer
sits on top of is what it takes. Then note heads, which win over sustain tails (a head sitting on
another note's tail takes the click), nearest onset center first among overlapping heads. Then
tails, resolving to the note whose tail rectangle contains the point, nearest onset first.

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
\return Boxed objects, notes first and each kind in its projection order.
*/
[[nodiscard]] std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom);

} // namespace rock_hero::editor::core
