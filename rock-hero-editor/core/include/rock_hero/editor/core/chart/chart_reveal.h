/*!
\file chart_reveal.h
\brief The tablature lane's reveal: whether the whole truth about one drawn thing is on show.

ONE REVEAL, one predicate PER SUBJECT. A NOTE draws to its ring end — every keyframe at its true
instant, its reveal-only marks with it — on two grounds, the lane reveal modifier and the note
being SELECTED (\ref chartNoteRevealed); a SPAN's furniture reads to its musical close on three,
the modifier and two positional ones (\ref chartSpanRevealed).

A REVEAL NEVER MOVES A TARGET. It adds ink, and the one mark that changes place under it — the
destination chip, standing at the crop while the ink cuts the leg toward its keyframe and giving
way to that keyframe's own mark at its instant once revealed — is never a target, so nothing a
gesture lands on moves because the gesture landed. That is what lets the selection be a ground.

Spelled here because two layers ask it, the lane that paints and the controller that hit-tests,
and a second spelling is how the drawn picture and the reachable one come apart.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Answers whether this note's whole truth is on show.

TWO GROUNDS, and either is enough. The LANE REVEAL is the modifier held over the whole lane, so
every visible note shows its truth while it is down. The SELECTION is the note being the thing
under scrutiny — the note itself, or any keyframe of it: a keyboard walk or a box that selects a
keyframe past the note's ink end must have a ring to show it, and the ring is drawn only where the
keyframe is. Both index lists are the selection resolved against the projection the lane draws
(\ref ChartEditViewState), which the lane holds as its published overlay and the controller
resolves afresh for a pointer event, through one resolver.

\param index Index of the note in the projection's note order.
\param lane_reveal True while the whole-lane reveal modifier is held.
\param selected_notes Ascending indices of the selected notes.
\param selected_keyframes The selected keyframes, ascending by note index.

\return True when this note draws to its ring end, and its reveal-only marks with it.
*/
[[nodiscard]] bool chartNoteRevealed(
    std::size_t index, bool lane_reveal, const std::vector<std::size_t>& selected_notes,
    const std::vector<ChartKeyframeRef>& selected_keyframes) noexcept;

/*!
\brief Answers whether this span's furniture runs to its musical close rather than its drawn extent.

THREE GROUNDS, and any one is enough. The LANE REVEAL is the modifier held over the whole lane, so
every visible span reads to its close while it is down, exactly as every visible note reads to its
real ring. A span COVERING A SELECTED NOTE reveals with it, because the selection is the thing under
scrutiny and the span a selected note stands in is part of what is being scrutinised — the same
argument that draws a selected note's own ring. And the CARET anywhere inside the span's tenure
reveals it, its STRING ignored, because a span is lane furniture rather than one string's ring —
which is why its position alone is asked for. Spans are not selectable in their own right; that
arrives with the span-marker work.

COVERAGE differs between the two positional arms, and the difference is principled. A selected
note's ONSET is judged start-included, close-EXCLUDED: an onset AT the close is the event that
ended the statement, or the one that opened the successor span, and neither is a member of this
span. The CARET is judged ends-INCLUDED: it is a position, not a member, so the membership argument
does not apply and a grid-snapped caret behaves the same
wherever it lands, so one sitting exactly on the close reveals, and at an abutting seam it reveals
both spans, the two true extents meeting. The close is the one instant that needs the rounding
tolerance (\ref common::core::g_onset_match_epsilon): equal grid positions resolve to equal
seconds, so the span's start compares exactly against an onset or the caret alike, but the close is
reached by adding the stored extent to the start rather than by resolving the closing head's own
position, and those two arithmetic paths to one instant may differ in the last bit — so the
selection arm pulls the close in by the tolerance and the caret arm pushes it out, each toward the
verdict its rule names for the boundary instant.

\param span The span whose furniture is being drawn.
\param lane_reveal True while the whole-lane reveal modifier is held.
\param notes The projection's notes, in the order \p selected_notes indexes.
\param selected_notes Ascending indices of the selected notes.
\param caret_seconds Where the caret stands on the arrangement timeline, when it stands in this
       lane at all.

\return True when this span's furniture runs to \ref common::core::ShapeViewState::close_seconds.
*/
[[nodiscard]] bool chartSpanRevealed(
    const common::core::ShapeViewState& span, bool lane_reveal,
    const std::vector<common::core::NoteViewState>& notes,
    const std::vector<std::size_t>& selected_notes,
    const std::optional<double>& caret_seconds) noexcept;

} // namespace rock_hero::editor::core
