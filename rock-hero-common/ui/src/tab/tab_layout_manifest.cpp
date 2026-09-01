#include "tab/tab_layout_manifest.h"

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

// Mirrors the paint core's drawNoteHead geometry: a square of note_height + 1 centered on
// (onset_x, laneY). The TAIL rectangle that stood beside it is gone with the target it served —
// heads are targets, tails are testimony (user ruling 2026-08-30) — and with it the one rectangle
// in this manifest that did not bound what the lane draws: it spanned the whole presented ring
// while a member under a span's ink draws no ribbon at all, so it claimed pixels nothing painted.
TabNoteLayout tabNoteLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept
{
    TabNoteLayout layout;
    layout.onset_x = geometry.x(note.start_seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.head_size = geometry.headSize();
    layout.head = centeredSquare(layout.onset_x, layout.center_y, layout.head_size);
    return layout;
}

// Mirrors the bracket pass's own rectangles: the pair's bars stand a bar-width apart from the
// head's ring on each side and rise to the head's visible edge less that same bar — and, where the
// projection printed this hold's own digit in the satellite column, out to cover that column too.
// The mark's DRAWN extent is its clickable one, which is what closes the drawn-digit-clicks-nowhere
// gap (user ruling 2026-08-27); the earlier box stopped at the bars and left a displaced digit
// reachable by nothing. Answers for a silent hold and nothing else, which is what its stop mark
// already says.
std::optional<TabSilentHoldLayout> tabSilentHoldLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept
{
    // Two facts, because a resolved stop mark no longer implies a hold: a note carrying a held stop
    // resolves one too, for the satellite beside the bars rather than for a face of its own. The
    // attack is what says whose face these bars are. Bound to a local so the optional check and
    // the accesses are provably the same object.
    const std::optional<common::core::StopMarkViewState>& mark = note.stop_mark;
    if (!common::core::silentHold(note.attack) || !mark.has_value())
    {
        return std::nullopt;
    }
    const TabBracketGeometry bracket = geometry.bracketGeometry();
    const float half_width = bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
    const float outboard = mark->slot == common::core::StopMarkSlot::Satellite
                               ? static_cast<float>(geometry.satelliteSlot().extent())
                               : 0.0f;
    TabSilentHoldLayout layout;
    layout.center_x = geometry.x(mark->seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.box = TabLayoutRect{
        .x = layout.center_x - half_width,
        .y = layout.center_y - bracket.half_height,
        .width = half_width * 2.0f + outboard,
        .height = bracket.half_height * 2.0f,
    };
    return layout;
}

// Mirrors the satellite column wherever one is drawn: it opens a gap past the closing bar's column
// and runs one slot wide, at the bracket's own height so the two halves of a bracketed mark present
// the same target. One rectangle for both anchors, because the mark carries the instant its own ink
// draws at — a fronting tap's digit sits beside its span's bracket, a note's own satellite beside
// its own head, and at a front those are the same column by construction.
//
// Answers for a note that states a held stop whose face is SHOWN — asked of the published mark
// rather than inferred from the held field, so the target can neither outlive the digit nor appear
// before a reveal brings it in.
std::optional<TabHeldStopLayout> tabHeldStopLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const bool revealed) noexcept
{
    // Each bound to a local so its check and its accesses are provably the same object.
    const std::optional<int>& held = note.held;
    const std::optional<common::core::StopMarkViewState>& mark = note.stop_mark;
    if (!held.has_value() || !mark.has_value() || !common::core::stopMarkShown(*mark, revealed))
    {
        return std::nullopt;
    }
    const TabBracketGeometry bracket = geometry.bracketGeometry();
    const TabSatelliteSlot slot = geometry.satelliteSlot();
    const float bar_right =
        geometry.x(mark->seconds) + bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
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
