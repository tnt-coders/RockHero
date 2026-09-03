#include <algorithm>
#include <cstddef>
#include <rock_hero/common/core/shared/visible_events.h>
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

// Rationale lives on the declaration in chart_reveal.h.
bool chartSpanRevealed(
    const common::core::ShapeViewState& span, const bool lane_reveal,
    const std::vector<common::core::NoteViewState>& notes,
    const std::vector<std::size_t>& selected_notes,
    const std::optional<ChartCaretPeek>& caret) noexcept
{
    if (lane_reveal)
    {
        return true;
    }
    // The caret is a POSITION, not a member, so unlike the selection arm below both of the span's
    // ends are inside it — the peek's precedent is that a grid-snapped caret behaves the same
    // wherever it lands — and the string is ignored, because a span is lane furniture rather than
    // one string's ring. The tolerance widens the close for the same two-arithmetic-paths reason
    // the selection arm narrows it: the boundary instant is decided by the rule, never by the
    // last bit.
    if (caret.has_value() && span.start_seconds <= caret->seconds &&
        caret->seconds <= span.close_seconds + common::core::g_onset_match_epsilon)
    {
        return true;
    }
    // The close is pushed inside by the rounding tolerance so an onset landing ON it — the event
    // that ended this statement, or the one that opened the successor — is outside the span it is
    // not a member of, whichever way the last bit of the two arithmetic paths falls.
    const double interior_end = span.close_seconds - common::core::g_onset_match_epsilon;
    return std::ranges::any_of(selected_notes, [&span, &notes, interior_end](std::size_t index) {
        // A selection is indices into the projection the lane last received, and the lane takes
        // its selection and its projection through separate setters, so the two can be a beat
        // apart; a stale index names no note rather than reading past the table.
        if (index >= notes.size())
        {
            return false;
        }
        const double onset = notes[index].start_seconds;
        return span.start_seconds <= onset && onset < interior_end;
    });
}

} // namespace rock_hero::editor::core
