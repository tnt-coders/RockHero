#include <algorithm>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/editor/core/chart/chart_reveal.h>

namespace rock_hero::editor::core
{

namespace
{

// The one caret-in-tenure rule both arms read: ends included, the end widened by the rounding
// tolerance (the \file block in chart_reveal.h says why).
[[nodiscard]] bool caretWithin(
    const std::optional<ChartCaretViewState>& caret, const double start_seconds,
    const double end_seconds) noexcept
{
    return caret.has_value() && start_seconds <= caret->seconds &&
           caret->seconds <= end_seconds + common::core::g_onset_match_epsilon;
}

} // namespace

// Rationale lives on the declaration in chart_reveal.h.
bool chartNoteRevealed(
    const std::vector<common::core::NoteViewState>& notes, const std::size_t index,
    const bool lane_reveal, const ChartEditViewState& edit) noexcept
{
    if (lane_reveal || std::ranges::binary_search(edit.selected_notes, index) ||
        std::ranges::binary_search(
            edit.selected_keyframes, index, std::ranges::less{}, &ChartKeyframeRef::note_index))
    {
        return true;
    }
    const common::core::NoteViewState& note = notes[index];
    // Bound to a local so the presence test and the read are provably the same object.
    const std::optional<ChartCaretViewState>& caret = edit.caret;
    return caret.has_value() && caret->string == note.string &&
           caretWithin(caret, note.start_seconds, note.ring_end_seconds);
}

// Rationale lives on the declaration in chart_reveal.h.
bool chartSpanRevealed(
    const common::core::ShapeViewState& span, const bool lane_reveal,
    const std::vector<common::core::NoteViewState>& notes, const ChartEditViewState& edit) noexcept
{
    if (lane_reveal || caretWithin(edit.caret, span.start_seconds, span.close_seconds))
    {
        return true;
    }
    const double interior_end = span.close_seconds - common::core::g_onset_match_epsilon;
    return std::ranges::any_of(
        edit.selected_notes, [&span, &notes, interior_end](std::size_t index) {
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
