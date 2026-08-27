#include "tab/tab_layout_manifest.h"

#include <algorithm>

namespace rock_hero::common::ui
{

namespace
{

// The one statement of "a mark's box is a square centred on its anchor" — the note head and the
// waypoint head are both drawn that way, and two copies of the halving would be free to disagree
// about which edge a click lands on.
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

// Mirrors the bracket pass's own rectangles: the pair's bars stand a bar-width apart from the
// head's ring on each side and rise to the head's visible edge less that same bar. Only the bars,
// deliberately — see the header for why the outboard digit is not part of the clickable extent.
std::optional<TabHoldMarkerLayout> tabHoldMarkerLayout(
    const TabLaneGeometry& geometry, const common::core::HoldMarkerViewState& marker) noexcept
{
    // Bound to a local so the optional check and the access are provably the same object.
    const std::optional<double>& bracket_seconds = marker.bracket_seconds;
    if (!bracket_seconds.has_value())
    {
        return std::nullopt;
    }
    const TabBracketGeometry bracket = geometry.bracketGeometry();
    const float half_width = bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
    TabHoldMarkerLayout layout;
    layout.center_x = geometry.x(*bracket_seconds);
    layout.center_y = geometry.laneY(marker.string);
    layout.box = TabLayoutRect{
        .x = layout.center_x - half_width,
        .y = layout.center_y - bracket.half_height,
        .width = half_width * 2.0f,
        .height = bracket.half_height * 2.0f,
    };
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
