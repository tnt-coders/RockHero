/*!
\file chart_reveal.h
\brief The tablature lane's reveal: whether the whole truth about one drawn thing is on show.

ONE REVEAL, one predicate PER SUBJECT, both reading the lane reveal modifier and the published
overlay (\ref ChartEditViewState: the selection and the caret, resolved against the projection the
lane draws — which the lane holds and the controller resolves afresh for a pointer event, through
one resolver). A NOTE draws to its ring end — every keyframe at its true instant, its reveal-only
marks with it — on three grounds: the modifier, the note being SELECTED, and the CARET standing
inside its ring (\ref chartNoteRevealed). A SPAN's furniture reads to its musical close on the
same three, read for a span (\ref chartSpanRevealed).

A REVEAL MOVES NO TARGET BUT THE ONE A PRESS NAMED. It adds ink, and the one mark that changes place
under it is the destination chip, standing at the crop while the ink cuts the leg toward its
keyframe and giving way to that keyframe's own mark at its instant once revealed. The chip is a face
of the keyframe it names (user report, 2026-09-28: a click on it must select it), so a press on it
selects that keyframe, whose selection reveals the note, and the mark moves to where the keyframe
really stands: the press's own object, carried to its truth, never some other target shifted
under the pointer. That is what still lets the selection and the caret be grounds.

THE CARET IS A POSITION, NOT A MEMBER, and both caret arms judge it ends-INCLUDED, so a
grid-snapped caret behaves the same wherever it lands: one sitting exactly on a ring's end or a
span's close is inside it, and at an abutting seam it is inside both. That end is the one instant
needing the rounding tolerance (\ref common::core::g_onset_match_epsilon): equal grid positions
resolve to equal seconds, so a start compares exactly against the caret, but a ring end or a close
is reached by adding a stored extent to the start rather than by resolving the ending instant's own
position, and those two arithmetic paths to one instant may differ in the last bit — so the caret
arms push the end out by the tolerance, toward the verdict the rule names. The span's selection arm
pulls the close IN by the same tolerance for the mirror reason (\ref chartSpanRevealed).

Spelled here because two layers ask it, the lane that paints and the controller that hit-tests,
and a second spelling is how the drawn picture and the reachable one come apart.
*/

#pragma once

#include <cstddef>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Answers whether this note's whole truth is on show.

THREE GROUNDS, and any one is enough. The LANE REVEAL is the modifier held over the whole lane, so
every visible note shows its truth while it is down. The SELECTION is the note being the thing
under scrutiny — the note itself, or any keyframe of it: a keyboard walk or a box that selects a
keyframe past the note's ink end must have a ring to show it, and the ring is drawn only where the
keyframe is. And THE CARET PEEK is the lane answering "is something here?" where the caret stands:
a caret on the note's string inside its STORED ring reveals it, ends included. Keyed on the edit
position alone — no timer, nothing latched — so the caret leaving is the whole of what hides the
ring again.

\param notes The projection's notes, in the order the selection indexes.
\param index Index of the note asked about, below `notes.size()`.
\param lane_reveal True while the whole-lane reveal modifier is held.
\param edit The published overlay: the selection resolved against the projection, and the caret.

\return True when this note draws to its ring end, and its reveal-only marks with it.
*/
[[nodiscard]] bool chartNoteRevealed(
    const std::vector<common::core::NoteViewState>& notes, std::size_t index, bool lane_reveal,
    const ChartEditViewState& edit) noexcept;

/*!
\brief Answers whether this span's furniture runs to its musical close rather than its drawn extent.

THREE GROUNDS, and any one is enough. The LANE REVEAL is the modifier held over the whole lane, so
every visible span reads to its close while it is down, exactly as every visible note reads to its
real ring. A span COVERING A SELECTED NOTE reveals with it, because the selection is the thing under
scrutiny and the span a selected note stands in is part of what is being scrutinised — the same
argument that draws a selected note's own ring. And the CARET anywhere inside the span's tenure
reveals it, its STRING ignored, because a span is lane furniture rather than one string's ring.
Spans are not selectable in their own right; that arrives with the span-marker work.

The selection arm judges a selected note's ONSET start-included, close-EXCLUDED: an onset AT the
close is the event that ended the statement, or the one that opened the successor span, and neither
is a member of this span. The close is pulled in by the rounding tolerance so an onset landing on
it is outside whichever way the last bit of the two arithmetic paths falls.

\param span The span whose furniture is being drawn.
\param lane_reveal True while the whole-lane reveal modifier is held.
\param notes The projection's notes, in the order the selection indexes.
\param edit The published overlay: the selection resolved against the projection, and the caret.

\return True when this span's furniture runs to \ref common::core::ShapeViewState::close_seconds.
*/
[[nodiscard]] bool chartSpanRevealed(
    const common::core::ShapeViewState& span, bool lane_reveal,
    const std::vector<common::core::NoteViewState>& notes, const ChartEditViewState& edit) noexcept;

} // namespace rock_hero::editor::core
