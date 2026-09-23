/*!
\file chart_hit_testing.h
\brief Headless hit resolution mapping tablature-lane pixels to chart objects.

**HEADS ARE TARGETS; TAILS ARE TESTIMONY**. A note is addressed at the one column where it happens,
and a tail states how long a string rings — evidence, not a handle. Every rectangle comes from the
shared layout manifest, computed from the same TabLaneGeometry the paint core drew with, so hit
policy can never drift from the rendered pixels: every mark a pointer can reach is one the lane
draws, and nothing undrawn is reachable.
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
\brief A note's HELD-stop satellite the pointer resolved, by index into the projection's notes.

The same note a \ref ChartNoteHit names, reached through its other mark: a distinct alternative
because the two address different stops of it. Selecting is identical — one note, no new selection
kind — and what the satellite adds is the CHANNEL, so the digits that follow state the stop the
charter actually clicked. That makes clicking it shorthand for selecting the note and pressing the
hold verb on it.
*/
struct ChartHeldStopHit
{
    /*! \brief Index into \ref common::core::ChartViewState::notes. */
    std::size_t index{0};

    /*!
    \brief Compares two held-stop hits by their stored values.
    \param lhs Left-hand hit.
    \param rhs Right-hand hit.
    \return True when both name the same note's held stop.
    */
    friend constexpr bool operator==(
        const ChartHeldStopHit& lhs, const ChartHeldStopHit& rhs) noexcept = default;
};

/*!
\brief A keyframe the pointer resolved: which projected note, and which of its drawn keyframes.

Two indices rather than one, which is why the hit target is a sum: a keyframe belongs to a note,
so no single index into a flat array names it.
*/
struct ChartKeyframeHit
{
    /*! \brief Index into \ref common::core::ChartViewState::notes. */
    std::size_t note_index{0};

    /*! \brief Index into that note's \ref common::core::NoteViewState::slides. */
    std::size_t keyframe_index{0};

    /*!
    \brief Compares two keyframe hits by their stored values.
    \param lhs Left-hand hit.
    \param rhs Right-hand hit.
    \return True when both name the same keyframe.
    */
    friend constexpr bool operator==(
        const ChartKeyframeHit& lhs, const ChartKeyframeHit& rhs) noexcept = default;
};

/*!
\brief One selectable object the lane resolved under a pointer.

Addressed by projection index instead of by identity: the controller turns one into the other, which
is the single place a drawn glyph becomes a selectable object. One alternative more than
\ref ChartSelectionKey has, deliberately: a note's held stop is not a second SELECTABLE object — it
selects the note like any other mark of it — but it is a second TARGET, and which one the pointer
landed on is exactly what the controller needs to know to point the next typed digit at the stop
that was clicked.
*/
using ChartHitTarget = std::variant<ChartNoteHit, ChartHeldStopHit, ChartKeyframeHit>;

/*!
\brief Resolves the selectable object under a lane-local point, if any.

Topmost drawn wins, which is the rule and the reason for the order below. Held-stop satellites
resolve first: they are drawn outboard of a bracket's closing bar and overlap no head of their own
note, so their position here is only about reaching them before a neighbouring head's box does.
Then note heads, nearest onset center first among overlapping heads. Then the linked keyframe heads
riding a tail, which are drawn ON the ribbon and are the last mark a pointer can reach.

A TAIL resolves to nothing at all. Selecting a note by a spot where it does not happen put the
selection where the caret was not, so a click on a ribbon falls through to the ordinary empty-slot
placement, and "is something here?" is answered by the lane reveal instead — one meaning per
click. The affordance this retires is selecting a long sustain whose head has
scrolled off-screen by clicking its tail; the marquee and the keyboard still reach it, and it is
recorded as a sighting item (`docs/tracking/watch-items.md`).

What the lane draws nothing for is not hit-testable, because nothing undrawn is. A keyframe is
reached only while it is DRAWN (\ref common::core::keyframeDrawn): one standing past its note's ink
end is reached only while the note is revealed, exactly as the lane draws it — a linked head at a
junction, the slide-out's chip at its end. A keyframe stating no fret draws nothing at all today —
how those should draw, and therefore how a pointer should reach them, is the bend display study's
question and not this function's.

A held stop's SATELLITE is reachable exactly while it is drawn, which for a reveal-only one is
exactly while the lane reveal is held: the layout manifest answers both questions from one
rectangle, so the two cannot part. A revealed note's extra tail length is not itself a target,
because a tail is not a target at all.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param x Pointer x in lane-local pixels.
\param y Pointer y in lane-local pixels.
\param revealed True while the lane reveal modifier is held, which is the whole of what shows a
       note's truth; a caller with no reveal state says false and reaches nothing undrawn.
\return The hit object, or empty for an empty-lane point.
*/
[[nodiscard]] std::optional<ChartHitTarget> chartHitTarget(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y, bool revealed = false);

/*!
\brief Collects the objects whose head or mark rectangles intersect a marquee box.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param left Left edge of the box in lane-local pixels.
\param top Top edge of the box in lane-local pixels.
\param right Right edge of the box in lane-local pixels.
\param bottom Bottom edge of the box in lane-local pixels.
\param revealed True while the lane reveal modifier is held, exactly as for \ref chartHitTarget: a
       keyframe past its note's ink end is boxed only while it is drawn.
\return Boxed objects: heads first, then keyframes, each in projection order.
*/
[[nodiscard]] std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom, bool revealed = false);

} // namespace rock_hero::editor::core
