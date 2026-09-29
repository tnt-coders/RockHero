/*!
\file chart_reveal.h
\brief The tablature lane's reveal: whether the whole truth about one drawn thing is on show.

ONE ANSWER PER SUBJECT, each reading the lane reveal modifier and the published overlay
(\ref ChartEditViewState: the selection and the caret, resolved against the projection the lane
draws — which the lane holds and the controller resolves afresh for a pointer event, through one
resolver). A NOTE draws to its ring end — every keyframe at its true instant, its reveal-only marks
with it — on three grounds: the modifier, the note being SELECTED, and the CARET standing on it
(\ref chartPresence). A SPAN's furniture reads to its musical close on the same three, read for a
span (\ref chartSpanRevealed).

THE LAST TWO ARE THE EDIT'S FOCUS, and focus does one thing the modifier does not: a focused ring
ending on the next head of its own string sets that head STEPPED BACK, drawn faint and beneath the
ring, so the ring being edited reads whole — its end's chips and dots in truth — instead of
fighting the head for one column (user ruling, 2026-09-28). A focused note never steps back, and
the modifier steps nothing back: it shows every ring at once, where no one ring is the subject.

A REVEAL CARRIES ITS MARKS TO THEIR TRUTH. It adds ink, running the tail on from its crop to its
ring's end, and the marks riding the tail travel with it: the destination chip at the crop glides
to its point's own instant, and a linked stop's chip gives way to its head there. The lane EASES the
run (common::ui::TabNotePresence), so a chip pressed at the crop visibly glides from under the
pointer to its truth (user ruling, 2026-09-28). The chip is a face of the keyframe it names, so a
press on it selects that keyframe, whose selection reveals the note: the press's own object carried
to its truth, never some other target shifted under the pointer. A press lands on the state the
ease is heading for, since the hit test reads this answer and never the eased picture. That is what
still lets the selection and the caret be grounds.

THE CARET IS A POSITION, NOT A MEMBER, and both caret arms judge it ends-INCLUDED, so a
grid-snapped caret behaves the same wherever it lands: one sitting exactly on a ring's end or a
span's close is inside it, and at an abutting span seam it is inside both. The one exception is a
ring ending ON the next head of its string: a caret there is on that head, the note it names, and
not on the ring ending under it, so that end is EXCLUDED. An end is the one instant needing the
rounding tolerance (\ref common::core::g_onset_match_epsilon): equal grid positions resolve to equal
seconds, so a start compares exactly against the caret, but a ring end or a close is reached by
adding a stored extent to the start rather than by resolving the ending instant's own position, and
those two arithmetic paths to one instant may differ in the last bit — so the caret arms move the
end by the tolerance, toward the verdict the rule names. The span's selection arm pulls the close
IN by the same tolerance for the mirror reason (\ref chartSpanRevealed).

The seam exception is display only. The keyframe commit law's own attention (the controller's
chartNoteInFocus) still counts a caret at the seam on the ring ending there, so an end statement
typed there is not dissolved as silent before the caret moves on.

Spelled here because two layers ask it, the lane that paints and the controller that hit-tests,
and a second spelling is how the drawn picture and the reachable one come apart.
*/

#pragma once

#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Answers how each note is presented: revealed or not, stepped back or not.

THE EDIT'S FOCUS has two grounds, and either is enough. The SELECTION is the note being the thing
under scrutiny — the note itself, or any keyframe of it: a keyboard walk or a box that selects a
keyframe past the note's ink end must have a ring to show it, and the ring is drawn only where the
keyframe is. And THE CARET PEEK is the lane answering "is something here?" where the caret stands:
a caret on the note's string inside its STORED ring is on the note, ends included — except where
the ring ends on the next head of its string, whose note the caret there is on instead. Keyed on the
edit position alone — no timer, nothing latched — so the caret leaving is the whole of what
unfocuses it again. The caret speaks only while the selection is empty: armed on a head or a
keyframe, the object it stands on IS the selection (the controller's armed-caret invariant), and at
a shared instant only the selection can say whether that is the head or the previous ring's end
statement.

A focused note is REVEALED, and so is every note while the LANE REVEAL, the modifier held over the
whole lane, is down. A focused ring ending on the next head of its own string
(\ref common::core::NoteViewState::end_head) steps that head BACK, unless the focus is on it too.

Asked once per change of its inputs rather than per note, since the caret's note is found by a
pass over the projection.

\param notes The projection's notes, in the order the selection indexes.
\param lane_reveal True while the whole-lane reveal modifier is held.
\param edit The published overlay: the selection resolved against the projection, and the caret.
       A selection index past the notes names no note.

\return One presence per note, in the notes' order, every amount at 0 or 1.
*/
[[nodiscard]] std::vector<common::ui::TabNotePresence> chartPresence(
    const std::vector<common::core::NoteViewState>& notes, bool lane_reveal,
    const ChartEditViewState& edit);

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
