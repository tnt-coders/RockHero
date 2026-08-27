#include "tab/tab_layout_manifest.h"

#include <algorithm>

namespace rock_hero::common::ui
{

namespace
{

// The one statement of "a mark's box is a square centred on its anchor" — the note head, the
// waypoint head and the hold mark are all drawn that way, and three copies of the halving would
// be free to disagree about which edge a click lands on.
[[nodiscard]] TabLayoutRect centeredSquare(
    const float center_x, const float center_y, const float size) noexcept
{
    return TabLayoutRect{
        .x = center_x - size / 2.0f,
        .y = center_y - size / 2.0f,
        .width = size,
        .height = size,
    };
}

} // namespace

// Mirrors the paint core's drawNoteHead / drawNoteTail geometry: the head is a square of
// note_height + 1 centered on (onset_x, laneY); the tail spans onset to the note's presented end
// across Charter's tail top/bottom around the string line. Both read that end off the note itself,
// so the hit rectangle cannot say a different length than the ribbon — the divergence that once
// shipped a drawn ribbon nothing could click.
TabNoteLayout tabNoteLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept
{
    TabNoteLayout layout;
    layout.onset_x = geometry.x(note.start_seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.head_size = geometry.headSize();
    layout.head = centeredSquare(layout.onset_x, layout.center_y, layout.head_size);

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

TabHoldMarkerLayout tabHoldMarkerLayout(
    const TabLaneGeometry& geometry, const common::core::HoldMarkerViewState& marker) noexcept
{
    TabHoldMarkerLayout layout;
    layout.center_x = geometry.x(marker.seconds);
    layout.center_y = geometry.laneY(marker.string);
    layout.extent = geometry.holdMarkerSize();
    layout.box = centeredSquare(layout.center_x, layout.center_y, layout.extent);
    return layout;
}

// Mirrors drawSlideWaypointHeadShape: the linked head is the note's own head shape at the note's
// own head size, centred on the waypoint's instant and the note's string line. Same square as the
// onset head, one column along the tail.
TabWaypointLayout tabWaypointLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::SlideViewState& waypoint) noexcept
{
    TabWaypointLayout layout;
    layout.center_x = geometry.x(waypoint.seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.head_size = geometry.headSize();
    layout.head = centeredSquare(layout.center_x, layout.center_y, layout.head_size);
    return layout;
}

} // namespace rock_hero::common::ui
