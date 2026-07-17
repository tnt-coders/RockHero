/*!
\file chart_hit_testing.h
\brief Headless hit resolution mapping tablature-lane pixels to chart notes.

Every rectangle comes from the shared layout manifest, computed from the same TabLaneGeometry
the paint core drew with, so hit policy can never drift from the rendered pixels.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/tab/tab_view_state.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Resolves the note under a lane-local point, if any.

Note heads win over sustain tails (a head sitting on another note's tail takes the click), and
among overlapping heads the one whose onset center is nearest the point wins. Tail hits resolve
to the note whose tail rectangle contains the point, nearest onset first.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param x Pointer x in lane-local pixels.
\param y Pointer y in lane-local pixels.
\return Index of the hit note in the projection's note order, or empty for an empty-lane point.
*/
[[nodiscard]] std::optional<std::size_t> chartNoteHitIndex(
    const common::core::TabViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y);

/*!
\brief Collects the notes whose head rectangles intersect a marquee box.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param left Left edge of the box in lane-local pixels.
\param top Top edge of the box in lane-local pixels.
\param right Right edge of the box in lane-local pixels.
\param bottom Bottom edge of the box in lane-local pixels.
\return Ascending indices of boxed notes in the projection's note order.
*/
[[nodiscard]] std::vector<std::size_t> chartNoteIndicesInBox(
    const common::core::TabViewState& tab, const common::ui::TabLaneGeometry& geometry, float left,
    float top, float right, float bottom);

} // namespace rock_hero::editor::core
