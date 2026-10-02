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
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
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
\brief A keyframe the pointer resolved: which projected note, and which of its drawn keyframes.

Two indices rather than one, which is why the hit target is a sum: a keyframe belongs to a note,
so no single index into a flat array names it.
*/
struct ChartKeyframeHit
{
    /*! \brief Index into \ref common::core::ChartViewState::notes. */
    std::size_t note_index{0};

    /*! \brief Index into that note's \ref common::core::NoteViewState::keyframes. */
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
\brief The chip printing an object's bend the pointer resolved: the onset's chip names its note, a
keyframe's chip its keyframe.

A face of the object that owns it: it selects that object like any other mark of it, and what it
adds is the FACE, so `Delete` takes the bend the charter clicked.
*/
struct ChartBendChipHit
{
    /*! \brief The object whose bend the chip prints. */
    std::variant<ChartNoteHit, ChartKeyframeHit> owner{ChartNoteHit{}};

    /*!
    \brief Compares two bend-chip hits by their stored values.
    \param lhs Left-hand hit.
    \param rhs Right-hand hit.
    \return True when both name the same object's chip.
    */
    friend constexpr bool operator==(
        const ChartBendChipHit& lhs, const ChartBendChipHit& rhs) noexcept = default;
};

/*!
\brief One selectable object the lane resolved under a pointer.

Addressed by projection index instead of by identity: the controller turns one into the other, which
is the single place a drawn glyph becomes a selectable object. One alternative more than
\ref ChartSelectionKey has, deliberately: an object's bend chip is not a second SELECTABLE object —
it selects its object like any other mark of it — but it is a second TARGET, and which one the
pointer landed on is exactly what the controller needs to know to put the caret on the face that
was clicked.
*/
using ChartHitTarget = std::variant<ChartNoteHit, ChartKeyframeHit, ChartBendChipHit>;

/*!
\brief Resolves the selectable object under a lane-local point, if any.

Topmost drawn wins, which is the rule and the reason for the order below. Note heads first, nearest
onset center first among overlapping heads. Then the linked keyframe heads riding a tail, which are
drawn ON the ribbon and are the last mark a pointer can reach. A note stepped back behind the ring
being edited goes through the same order after every note in front.

A TAIL resolves to nothing at all. Selecting a note by a spot where it does not happen would put
the selection where the caret is not, so a click on a ribbon falls through to the ordinary
empty-slot placement, and "is something here?" is answered by the lane reveal instead — one
meaning per click. The affordance given up is selecting a long sustain whose head has scrolled
off-screen by clicking its tail; the marquee and the keyboard still reach it, and the trade is
tracked in `docs/tracking/watch-items.md`.

What the lane draws nothing for is not hit-testable, because nothing undrawn is. A keyframe is
reached where the lane draws a face of it (\ref common::ui::TabKeyframeLayout): its mark within the
extent the note is drawn to — a linked head, a slide-out's chip, a dot on the bend curve — the chip
printing the bend it states, and, past the extent, the destination chip a cut leg wears at the crop,
which names it. Selecting it through that chip reveals the note, so the keyframe then draws at its
true instant.

A held stop's SATELLITE is reachable exactly while it is drawn, which for a reveal-only one is
exactly while its note is revealed: the layout manifest answers both questions from one rectangle,
so the two cannot part. A revealed note's extra tail length is not itself a target, because a tail
is not a target at all.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param x Pointer x in lane-local pixels.
\param y Pointer y in lane-local pixels.
\param presence Per-note presentation, settled (\ref common::ui::TabNotePresence), the answer the
       lane's picture heads for: a revealed note reaches its reveal-only marks, and a note stepped
       back answers after every note in front of it, as it is drawn beneath them. Empty presents
       every note plainly and reaches nothing undrawn.
\return The hit object, or empty for an empty-lane point.
*/
[[nodiscard]] std::optional<ChartHitTarget> chartHitTarget(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y, const common::ui::TabPresence& presence = {});

/*!
\brief Collects the objects whose head or mark rectangles intersect a marquee box.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param left Left edge of the box in lane-local pixels.
\param top Top edge of the box in lane-local pixels.
\param right Right edge of the box in lane-local pixels.
\param bottom Bottom edge of the box in lane-local pixels.
\param presence Per-note presentation, exactly as for \ref chartHitTarget: a keyframe past its
       note's ink end is boxed only while it is drawn.
\return Boxed objects: heads first, then keyframes, each in projection order.
*/
[[nodiscard]] std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom, const common::ui::TabPresence& presence = {});

} // namespace rock_hero::editor::core
