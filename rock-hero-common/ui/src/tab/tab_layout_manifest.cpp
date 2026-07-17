#include "tab/tab_layout_manifest.h"

#include <algorithm>

namespace rock_hero::common::ui
{

// Mirrors the paint core's drawNoteHead / drawNoteTail geometry: the head is a square of
// note_height + 1 centered on (onset_x, laneY); the tail spans onset to sustain end across
// Charter's tail top/bottom around the string line.
TabNoteLayout tabNoteLayout(
    const TabLaneGeometry& geometry, const common::core::TabNoteView& note) noexcept
{
    TabNoteLayout layout;
    layout.onset_x = geometry.x(note.start_seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.head_size = geometry.note_height + 1.0f;
    layout.head = TabLayoutRect{
        .x = layout.onset_x - layout.head_size / 2.0f,
        .y = layout.center_y - layout.head_size / 2.0f,
        .width = layout.head_size,
        .height = layout.head_size,
    };

    const TailSpan span = tailSpan(geometry, layout.center_y);
    const float end_x = geometry.x(note.end_seconds);
    layout.tail = TabLayoutRect{
        .x = layout.onset_x,
        .y = span.top,
        .width = std::max(0.0f, end_x - layout.onset_x),
        .height = span.bottom - span.top,
    };
    return layout;
}

} // namespace rock_hero::common::ui
