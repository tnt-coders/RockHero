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

TWO GROUNDS, and either is enough (user ruling 2026-09-04). The LANE REVEAL is the modifier held
over the whole lane, so every visible span reads to its close while it is down, exactly as every
visible note reads to its real ring. And a span COVERING A SELECTED NOTE reveals with it, because
the selection is the thing under scrutiny and the span a selected note stands in is part of what is
being scrutinised — the same argument that draws a selected note's own ring.

There is no caret arm, and the subject is why: the peek answers "is something here?" about one note
on one string by measuring the caret against that note's ring (\ref chartNoteRevealed), and a span
is neither on a string nor a thing the caret can stand inside in that sense. Spans are not
selectable in their own right either; that arrives with the span-marker work.

COVERAGE is a selected note's ONSET inside the span — its start included, its close EXCLUDED. An
onset AT the close is the event that ended the statement, or the one that opened the successor span,
and neither is a member of this span. The close is the one comparison here that needs the rounding
tolerance (\ref common::core::g_onset_match_epsilon): equal grid positions resolve to equal seconds,
so a member sitting exactly on the span's start compares exactly, but the close is reached by adding
the stored extent to the start rather than by resolving the closing head's own position, and those
two arithmetic paths to one instant may differ in the last bit.

\param span The span whose furniture is being drawn.
\param lane_reveal True while the whole-lane reveal modifier is held.
\param notes The projection's notes, in the order \p selected_notes indexes; either form serves,
       since presentation moves no onset.
\param selected_notes Ascending indices of the selected notes.

\return True when this span's furniture runs to \ref common::core::ShapeViewState::close_seconds.
*/
[[nodiscard]] bool chartSpanRevealed(
    const common::core::ShapeViewState& span, bool lane_reveal,
    const std::vector<common::core::NoteViewState>& notes,
    const std::vector<std::size_t>& selected_notes) noexcept;

} // namespace rock_hero::editor::core
