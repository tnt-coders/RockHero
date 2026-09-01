#include <rock_hero/editor/core/chart/chart_reveal.h>

namespace rock_hero::editor::core
{

bool chartNoteRevealed(
    const common::core::NoteViewState& actual_note, const bool lane_reveal, const bool selected,
    const std::optional<ChartCaretPeek>& caret) noexcept
{
    if (lane_reveal || selected)
    {
        return true;
    }
    // Bound to a local so the presence test and the reads are provably the same object.
    if (!caret.has_value())
    {
        return false;
    }
    const ChartCaretPeek& peek = *caret;
    // The STORED ring, ends included — the whole of the peek. A caret landing exactly on a ring's
    // end is inside it like any other position rather than a strictness question, which is what
    // makes a grid-snapped caret behave the same wherever it lands.
    return peek.string == actual_note.string && actual_note.start_seconds <= peek.seconds &&
           peek.seconds <= actual_note.end_seconds;
}

} // namespace rock_hero::editor::core
