#include "tab/tab_layout_manifest.h"

#include <algorithm>
#include <cstddef>
#include <rock_hero/common/core/shared/overloaded.h>
#include <variant>

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
// (onset_x, laneY). No TAIL rectangle stands beside it, because heads are targets and tails are
// testimony — and because a tail rectangle would be the one rectangle in this manifest that does
// not bound what the lane draws: it spans the whole inked ring while a member under a span's
// ink draws no ribbon at all, so it would claim pixels nothing painted.
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
// own head size, centred on the stop's instant and the note's string line. Same square as the
// onset head, one column along the tail. A chip — the slide-out's at its instant, the destination
// chip at the crop — sits a third of a head above the tail envelope when the leg into it rises and
// below it when it falls, or on the side the shared instant gives it (both from one authority),
// and its box is the chip's ground: the fret text height with the chip's one-pixel margins, and
// the two-digit width the satellite column already states for this lane's digits.
TabKeyframeLayout tabSlideStopLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const std::size_t stop, const double drawn_end) noexcept
{
    const common::core::SlideStopViewState& slide = note.slides[stop];
    const bool drawn = common::core::instantDrawn(slide.seconds, drawn_end);
    TabKeyframeLayout layout;
    layout.center_x = geometry.x(std::min(slide.seconds, drawn_end));
    layout.center_y = geometry.laneY(note.string);
    layout.head_size = geometry.headSize();
    if (drawn && common::core::linkedKeyframe(slide))
    {
        layout.box = centeredSquare(layout.center_x, layout.center_y, layout.head_size);
        return layout;
    }
    // The leg into the chip rises when the stop's fret is at or above the stop before it — the
    // previous stop's, or the onset's when it is the first.
    const int previous_fret = stop == 0 ? note.fret : note.slides[stop - 1].fret;
    const bool upward = slide.fret >= previous_fret;
    layout.shape = TabKeyframeShape::Chip;
    // Both bands are the shared authority's (slideOutChipY, endMarkYAtSharedInstant), so the box
    // the click is bounded in cannot land on the other side of the envelope from the chip. Only a
    // DRAWN chip can stand at the shared instant: a chip at the crop stands a margin before it.
    layout.center_y =
        endMarkYAtSharedInstant(geometry, layout.center_y, drawn && note.ends_on_next_head)
            .value_or(slideOutChipY(geometry, layout.center_y, upward));
    const float text_height = geometry.fretTextHeight();
    const float width = text_height * 1.4f + 6.0f;
    const float height = text_height + 2.0f;
    layout.box = TabLayoutRect{
        .x = layout.center_x - width / 2.0f,
        .y = layout.center_y - height / 2.0f,
        .width = width,
        .height = height,
    };
    return layout;
}

// A stop's keyframe defers to the stop's own layout. A bend-only point's dot stands where
// drawBendDots fills it, on the curve at the amount it states; a resting keyframe's head is the
// linked head at its instant, exactly as a stop's.
TabKeyframeLayout tabKeyframeLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe, const double drawn_end)
{
    // The mark's own instant, on the string line: where a head stands, and the column a dot rides.
    TabKeyframeLayout at_instant;
    at_instant.center_x = geometry.x(std::min(keyframe.seconds, drawn_end));
    at_instant.center_y = geometry.laneY(note.string);
    at_instant.head_size = geometry.headSize();
    return std::visit(
        common::core::Overloaded{
            [&](const common::core::KeyframeStopMark& stop) {
                return tabSlideStopLayout(geometry, note, stop.stop, drawn_end);
            },
            [&](const common::core::KeyframeBendMark& bend) {
                TabKeyframeLayout layout = at_instant;
                layout.center_y = bendCurveY(geometry, layout.center_y, bend.semitones);
                layout.shape = TabKeyframeShape::Dot;
                layout.box =
                    centeredSquare(layout.center_x, layout.center_y, layout.head_size / 2.0f);
                return layout;
            },
            [&](const common::core::KeyframeRestMark&) {
                TabKeyframeLayout layout = at_instant;
                layout.box = centeredSquare(layout.center_x, layout.center_y, layout.head_size);
                return layout;
            },
        },
        keyframe.mark);
}

} // namespace rock_hero::common::ui
