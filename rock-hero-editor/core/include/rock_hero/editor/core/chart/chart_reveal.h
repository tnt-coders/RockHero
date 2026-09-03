/*!
\file chart_reveal.h
\brief The tablature lane's reveal: whether the whole truth about one drawn thing is on show.

ONE REVEAL, one predicate PER SUBJECT. The lane shows a note's real ring in place of the presented
picture on three grounds — the reveal modifier held over the whole lane, the note being SELECTED,
and the caret standing inside its ring — and the same answer decides whether that note's
reveal-only marks are there (\ref common::core::stopMarkShown): revealing a note shows the whole
truth about it at once. A SPAN's furniture reads to its musical close on the same terms, minus the
caret arm the subject cannot carry (\ref chartSpanRevealed).

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
\brief Where the caret stands, in the two facts the reveal reads.

Deliberately not the caret view state itself: this is asked from the controller as well as from the
lane, and the controller's caret is a musical slot rather than a published state. Two fields, both
of which the peek needs, so a caller cannot supply half an answer.
*/
struct ChartCaretPeek
{
    /*! \brief Caret position in seconds on the arrangement timeline. */
    double seconds{0.0};

    /*! \brief One-based string the caret sits on, counted from the lowest-pitched string. */
    int string{1};
};

/*!
\brief Answers whether this note's whole truth is on show.

The three grounds, and any of them is enough. The LANE REVEAL is the modifier held over the whole
lane, so every visible note shows its truth while it is down. The SELECTION is the note being the
thing under scrutiny. And the CARET PEEK is the lane answering "is something here?" where the caret
stands: a caret inside a note's stored ring reveals that note, ends included, which is the whole of
the rule — it is asked of the ring rather than of the drawn tail so a ribbon hidden for any reason
still answers.

Asked of the note in its \ref common::core::ChartNoteForm::Actual form, because the ring the peek
measures is the stored one; the presented form's shorter tail would make the peek disagree with the
reveal it triggers.

\param actual_note The note in its actual form — the one whose ring the peek measures.
\param lane_reveal True while the whole-lane reveal modifier is held.
\param selected True when this note is in the selection.
\param caret Where the caret stands, or nothing while none is armed.

\return True when this note's real ring, and its reveal-only marks with it, are on show.
*/
[[nodiscard]] bool chartNoteRevealed(
    const common::core::NoteViewState& actual_note, bool lane_reveal, bool selected,
    const std::optional<ChartCaretPeek>& caret) noexcept;

/*!
\brief Answers whether this span's furniture runs to its musical close rather than its drawn extent.

THREE GROUNDS, and any one is enough (user rulings 2026-09-04). The LANE REVEAL is the modifier
held over the whole lane, so every visible span reads to its close while it is down, exactly as
every visible note reads to its real ring. A span COVERING A SELECTED NOTE reveals with it, because
the selection is the thing under scrutiny and the span a selected note stands in is part of what is
being scrutinised — the same argument that draws a selected note's own ring. And the CARET anywhere
inside the span's tenure reveals it — the convention that lets the caret peek a note's hidden tail
by standing where it lives — with the STRING ignored, because a span is lane furniture rather than
one string's ring. Spans are not selectable in their own right; that arrives with the span-marker
work.

COVERAGE differs between the two positional arms, and the difference is principled. A selected
note's ONSET is judged start-included, close-EXCLUDED: an onset AT the close is the event that
ended the statement, or the one that opened the successor span, and neither is a member of this
span. The CARET is judged ends-INCLUDED: it is a position, not a member, so the membership argument
does not apply and the peek's own precedent governs — a grid-snapped caret behaves the same
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
\param caret Caret peek position, when the caret stands in this lane.

\return True when this span's furniture runs to \ref common::core::ShapeViewState::close_seconds.
*/
[[nodiscard]] bool chartSpanRevealed(
    const common::core::ShapeViewState& span, bool lane_reveal,
    const std::vector<common::core::NoteViewState>& notes,
    const std::vector<std::size_t>& selected_notes,
    const std::optional<ChartCaretPeek>& caret) noexcept;

} // namespace rock_hero::editor::core
