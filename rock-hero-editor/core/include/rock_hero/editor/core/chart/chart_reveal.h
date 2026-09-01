/*!
\file chart_reveal.h
\brief The tablature lane's per-note reveal: whether one note's whole truth is on show.

ONE REVEAL, one predicate. The lane shows a note's real ring in place of the presented picture on
three grounds — the reveal modifier held over the whole lane, the note being SELECTED, and the caret
standing inside its ring — and the same answer decides whether that note's reveal-only marks are
there (\ref common::core::stopMarkShown): revealing a note shows the whole truth about it at once.
Spelled here because two layers ask it, the lane that paints and the controller that hit-tests and
types, and a second spelling is how the drawn picture and the reachable one come apart.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>

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

} // namespace rock_hero::editor::core
