/*!
\file chart_hit_testing.h
\brief Headless hit resolution mapping tablature-lane pixels to chart objects.

**HEADS ARE TARGETS; TAILS ARE TESTIMONY** (user ruling 2026-08-30). A note is addressed at the one
column where it happens, and a tail states how long a string rings — evidence, not a handle. Every
rectangle comes from the shared layout manifest, computed from the same TabLaneGeometry the paint
core drew with, so hit policy can never drift from the rendered pixels: every mark a pointer can
reach is one the lane draws, and nothing undrawn is reachable.
*/

#pragma once

#include "chart/chart_selection.h"

#include <cstddef>
#include <functional>
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
\brief Answers whether one projected note's whole truth is on show (\ref chartNoteRevealed).

Handed in rather than derived here, for the reason every input to this file is: hit resolution reads
the drawn picture, and WHICH notes are revealed is the controller's own state — the lane reveal, the
selection, and the caret. An empty accessor reveals nothing, so a caller with no reveal state says
so instead of a mark appearing under the pointer that nothing drew.
*/
using ChartNoteRevealed = std::function<bool(std::size_t index)>;

/*!
\brief Resolves the selectable object under a lane-local point, if any.

Topmost drawn wins, which is the rule and the reason for the order below — with ONE stated
exception. Silently-held stops resolve FIRST even though the paint core draws their brackets UNDER
the heads: a hold's bracket is its only affordance and no fretting-hand head of its own string is
drawn under it (a claim earns a face only on a string the span's sound never states, and the growth
law puts every fretting-hand sounding inside a span on a string it does state), so all the priority
takes is the near columns of a head a little later on that string, which the head can spare and a
two-pixel bracket bar cannot. The exception is recorded with the verb's design record rather than
left to be inferred from this order. Then held-stop satellites, which are drawn outboard of a
bracket's closing bar and overlap no head of their own note, so their position here is only about
reaching them before a neighbouring head's box does. Then note heads, nearest onset center first
among overlapping heads. Then the linked keyframe heads riding a tail, which are drawn ON the
ribbon and are the last mark a pointer can reach.

A TAIL resolves to nothing at all. Selecting a note by a spot where it does not happen put the
selection where the caret was not, so a click on a ribbon falls through to the ordinary empty-slot
placement and the lane answers "is something here?" by REVEALING the ring the caret sits inside —
one meaning per click. The affordance this retires is selecting a long sustain whose head has
scrolled off-screen by clicking its tail; the marquee and the keyboard still reach it, and it is
recorded as a sighting item (`docs/tracking/watch-items.md`).

Two objects the lane draws nothing for are not hit-testable, because nothing undrawn is. A keyframe
carries a head only when it is LINKED (\ref common::core::linkedKeyframe), and one stating no fret
draws nothing at all today — how those should draw, and therefore how a pointer should reach them,
is the bend display study's question and not this function's. A silent hold whose stop joined no
posture draws no bracket, which \ref common::ui::tabSilentHoldLayout answers with no layout at all,
so the skip needs no rule of its own here.

A held stop's SATELLITE is reachable exactly while it is drawn, which for a reveal-only one is
exactly while its note is revealed: the layout manifest answers both questions from one rectangle,
so the two cannot part. Everything else here is unaffected by the reveal — a revealed note's extra
tail length is deliberately not hit-testable (\ref EditorViewState::tab_actual), because a tail is
not a target at all.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param x Pointer x in lane-local pixels.
\param y Pointer y in lane-local pixels.
\param revealed Per-note answer to whether its whole truth is on show; empty reveals nothing.
\return The hit object, or empty for an empty-lane point.
*/
[[nodiscard]] std::optional<ChartHitTarget> chartHitTarget(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y, const ChartNoteRevealed& revealed = {});

/*!
\brief Collects the objects whose head or mark rectangles intersect a marquee box.

\param tab Seconds-resolved tab projection being displayed.
\param geometry Lane geometry the notation was painted with.
\param left Left edge of the box in lane-local pixels.
\param top Top edge of the box in lane-local pixels.
\param right Right edge of the box in lane-local pixels.
\param bottom Bottom edge of the box in lane-local pixels.
\return Boxed objects: heads first, then silent holds, then keyframes, each in projection order.
*/
[[nodiscard]] std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom);

} // namespace rock_hero::editor::core
