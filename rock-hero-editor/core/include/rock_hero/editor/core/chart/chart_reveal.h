/*!
\file chart_reveal.h
\brief The tablature lane's reveal: whether the whole truth about one drawn thing is on show.

A NOTE is revealed by the lane reveal modifier ALONE — a selected note and the caret's own note
draw like every other, so a clicked chip stays where it was drawn and the stored position shows
under the modifier that is held for every move of it. That answer is the lane's own and needs no
spelling here. A SPAN's furniture reads to its musical close on three grounds, the modifier and two
positional ones (\ref chartSpanRevealed), because a span has no second form to jump between.

Spelled here because two layers ask it, the lane that paints and the controller that hit-tests and
types, and a second spelling is how the drawn picture and the reachable one come apart.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

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
\param notes The projection's notes, in the order \p selected_notes indexes; either form serves,
       since presentation moves no onset.
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
