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

// Whether the caret stands on this note: on its string, inside its ring — the end excluded where
// the next head of the string starts there, since the caret there is on that head.
[[nodiscard]] bool caretOnNote(
    const std::optional<ChartCaretViewState>& caret,
    const common::core::NoteViewState& note) noexcept
{
    if (!caret.has_value() || caret->string != note.string)
    {
        return false;
    }
    if (note.end_head.has_value())
    {
        return note.start_seconds <= caret->seconds &&
               caret->seconds < note.ring_end_seconds - common::core::g_onset_match_epsilon;
    }
    return caretWithin(caret, note.start_seconds, note.ring_end_seconds);
}

} // namespace

// Rationale lives on the declaration in chart_reveal.h.
std::vector<common::ui::TabNotePresence> chartPresence(
    const std::vector<common::core::NoteViewState>& notes, const bool lane_reveal,
    const ChartEditViewState& edit)
{
    std::vector<common::ui::TabNotePresence> presence(notes.size());
    // The focus first, recorded as the reveal it grants. A selection is indices into the
    // projection the lane last received, and the lane takes its selection and its projection
    // through separate setters, so a stale index names no note rather than reading past the table.
    const auto focus = [&presence](const std::size_t index) {
        if (index < presence.size())
        {
            presence[index].reveal = 1.0f;
        }
    };
    for (const std::size_t index : edit.selected_notes)
    {
        focus(index);
    }
    for (const ChartKeyframeRef& keyframe : edit.selected_keyframes)
    {
        focus(keyframe.note_index);
    }
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        if (caretOnNote(edit.caret, notes[index]))
        {
            focus(index);
        }
    }
    // Every focused ring steps back the head it ends on unless that head is focused too, read
    // before the lane reveal joins, since the modifier steps nothing back.
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const std::optional<std::size_t>& end_head = notes[index].end_head;
        if (presence[index].revealing() && end_head.has_value() && !presence[*end_head].revealing())
        {
            presence[*end_head].recede = 1.0f;
        }
    }
    if (lane_reveal)
    {
        for (common::ui::TabNotePresence& note : presence)
        {
            note.reveal = 1.0f;
        }
    }
    return presence;
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
