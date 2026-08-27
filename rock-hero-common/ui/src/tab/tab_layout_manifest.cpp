#include "tab/tab_layout_manifest.h"

#include <algorithm>

namespace rock_hero::common::ui
{

namespace
{

// The one statement of "a mark's box is a square centred on its anchor" — the note head and the
// keyframe head are both drawn that way, and two copies of the halving would be free to disagree
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
// Answers for a silent hold and nothing else, which is what its bracket instant already says.
std::optional<TabSilentHoldLayout> tabSilentHoldLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept
{
    // Two facts, because a resolved bracket instant no longer implies a hold: a note carrying a
    // held stop resolves one too, for the satellite beside the bars rather than for a face of its
    // own. The attack is what says whose face these bars are. Bound to a local so the optional
    // check and the access are provably the same object.
    const std::optional<double>& bracket_seconds = note.bracket_seconds;
    if (!common::core::silentHold(note.attack) || !bracket_seconds.has_value())
    {
        return std::nullopt;
    }
    const TabBracketGeometry bracket = geometry.bracketGeometry();
    const float half_width = bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
    TabSilentHoldLayout layout;
    layout.center_x = geometry.x(*bracket_seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.box = TabLayoutRect{
        .x = layout.center_x - half_width,
        .y = layout.center_y - bracket.half_height,
        .width = half_width * 2.0f,
        .height = bracket.half_height * 2.0f,
    };
    return layout;
}

// Mirrors the bracket pass's side-slot rectangles: the column opens a gap past the closing bar and
// runs one slot wide, at the bracket's own height so the two halves of the mark present the same
// target. Answers for a note that states a held stop AND resolved a bracket to print it at, which
// together are exactly when the satellite is drawn.
std::optional<TabHeldStopLayout> tabHeldStopLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept
{
    // Each bound to a local so its check and its access are provably the same object.
    const std::optional<int>& held = note.held;
    const std::optional<double>& bracket_seconds = note.bracket_seconds;
    if (!held.has_value() || !bracket_seconds.has_value())
    {
        return std::nullopt;
    }
    const TabBracketGeometry bracket = geometry.bracketGeometry();
    const TabSatelliteSlot slot = geometry.satelliteSlot();
    const float bar_right =
        geometry.x(*bracket_seconds) + bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
    const auto width = static_cast<float>(slot.extent());
    TabHeldStopLayout layout;
    layout.center_x = bar_right + width / 2.0f;
    layout.center_y = geometry.laneY(note.string);
    layout.box = TabLayoutRect{
        .x = bar_right,
        .y = layout.center_y - bracket.half_height,
        .width = width,
        .height = bracket.half_height * 2.0f,
    };
    return layout;
}

// Mirrors drawKeyframeHeadShape: the linked head is the note's own head shape at the note's
// own head size, centred on the keyframe's instant and the note's string line. Same square as the
// onset head, one column along the tail.
TabKeyframeLayout tabKeyframeLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe) noexcept
{
    TabKeyframeLayout layout;
    layout.center_x = geometry.x(keyframe.seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.head_size = geometry.headSize();
    layout.head = centeredSquare(layout.center_x, layout.center_y, layout.head_size);
    return layout;
}

} // namespace rock_hero::common::ui
