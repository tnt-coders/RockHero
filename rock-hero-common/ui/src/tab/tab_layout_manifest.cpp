#include "tab/tab_layout_manifest.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <optional>
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

// The box of a bend chip centred on a column: half a tail above the curve at the amount it prints,
// or above the head where the column is the onset's. At the amount's own height rather than where
// a cut leg stops, so a chip riding a revealing leg glides level to its point.
[[nodiscard]] TabLayoutRect bendChipBox(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note, const float anchor_x,
    const double semitones) noexcept
{
    const float text_height = geometry.fretTextHeight();
    // The widest amount a chip prints, the slur, a whole step and a fraction ("2 3/4"), in
    // fret-text heights: it measures 2.9 at the shipped lane and 3.0 at the text floor, and the
    // margin is for other platforms' glyph widths. The paint core's tests hold every amount inside
    // it.
    constexpr float widest_text_heights = 3.2f;
    const float width = text_height * widest_text_heights + 6.0f;
    const float height = text_height + 2.0f;
    const float center_y = geometry.laneY(note.string);
    const bool over_head = anchor_x <= geometry.x(note.start_seconds) + geometry.note_height / 2.0f;
    const float chip_y =
        over_head ? center_y - geometry.note_height / 2.0f - text_height / 2.0f - 1.0f
                  : bendCurveY(geometry, center_y, semitones) - geometry.tail_height / 2.0f;
    return TabLayoutRect{
        .x = anchor_x - width / 2.0f,
        .y = chip_y - height / 2.0f,
        .width = width,
        .height = height,
    };
}

} // namespace

// Mirrors the paint core's drawNoteHead geometry: a square of note_height + 1 centered on
// (onset_x, laneY). No TAIL rectangle stands beside it, because heads are targets and tails are
// testimony — and because a tail rectangle would be the one rectangle in this manifest that does
// not bound what the lane draws: it spans the whole inked ring while a member under a span's
// ink draws no ribbon at all, so it would claim pixels nothing painted.
TabLayoutRect tabSlotHeadSquare(
    const TabLaneGeometry& geometry, const double seconds, const int chart_string) noexcept
{
    return centeredSquare(geometry.x(seconds), geometry.laneY(chart_string), geometry.headSize());
}

TabNoteLayout tabNoteLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept
{
    TabNoteLayout layout;
    layout.onset_x = geometry.x(note.start_seconds);
    layout.center_y = geometry.laneY(note.string);
    layout.head_size = geometry.headSize();
    layout.head = tabSlotHeadSquare(geometry, note.start_seconds, note.string);
    // The onset's own bend point opens the curve at the onset's instant; its chip stands above the
    // head. The extent passed is the ink's: an onset's chip never states the ring's end, so the
    // extent never reaches it.
    if (!note.bend.empty())
    {
        const common::core::BendPointViewState& onset = note.bend.front();
        if (std::is_eq(onset.seconds <=> note.start_seconds))
        {
            layout.bend_chip = tabBendPointChipBox(geometry, note, 0, note.ink_end_seconds);
        }
    }
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
// below it when it falls, ending short of the head where the ring ends on one (both from one
// authority), and its box is the chip's ground: the fret text height with the chip's one-pixel
// margins, and the two-digit width the satellite column already states for this lane's digits.
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
        layout.mark_drawn = true;
        layout.box = centeredSquare(layout.center_x, layout.center_y, layout.head_size);
        return layout;
    }
    // The leg into the chip rises when the stop's fret is at or above the stop before it — the
    // previous stop's, or the onset's when it is the first.
    const int previous_fret = stop == 0 ? note.fret : note.slides[stop - 1].fret;
    const bool upward = slide.fret >= previous_fret;
    layout.shape = TabKeyframeShape::Chip;
    // Whether the chip is drawn: within the extent it is a slide-out's (every other drawn stop wore
    // its linked head above). Past it only the first stop wears one, the DESTINATION chip naming
    // where the leg the ink cuts is heading, and only where that leg changes the fret and is no
    // shift slide's ARRIVAL, whose landing the next head already shows. A note whose ink stops at
    // its onset, or a lane that prints no text, wears none.
    const bool first_past =
        stop == 0 || common::core::instantDrawn(note.slides[stop - 1].seconds, drawn_end);
    const bool arrival =
        stop + 1 == note.slides.size() && note.end_head.has_value() && !slide.slide_out;
    layout.mark_drawn = geometry.draw_text && tailInked(note, drawn_end) &&
                        (drawn || (first_past && slide.fret != previous_fret && !arrival));
    // The band is the shared authority's (slideOutChipY), so the box the click is bounded in cannot
    // part from the chip the paint core centres on it.
    layout.center_y = slideOutChipY(geometry, layout.center_y, upward);
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

// Rationale lives on the declaration in tab_layout_manifest.h.
std::optional<TabLayoutRect> tabBendPointChipBox(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const std::size_t point, const double drawn_end) noexcept
{
    if (!geometry.draw_text)
    {
        return std::nullopt;
    }
    const common::core::BendPointViewState& into = note.bend[point];
    const TabBendLeg leg = tabBendLeg(geometry, note, point, drawn_end);
    if (leg.cut)
    {
        // Only the first point past the extent has a leg drawn toward it, and its chip names a
        // change of amount on a note that draws a tail at all.
        const bool earlier_cut =
            point > 0 && !common::core::instantDrawn(note.bend[point - 1].seconds, drawn_end);
        const double from_semitones = point == 0 ? 0.0 : note.bend[point - 1].semitones;
        if (earlier_cut || std::is_eq(into.semitones <=> from_semitones) ||
            !tailInked(note, drawn_end))
        {
            return std::nullopt;
        }
    }
    return bendChipBox(geometry, note, leg.to_x, into.semitones);
}

// A stop's keyframe defers to the stop's own layout. A point riding the curve stands where
// drawBendDots fills its dot, where the drawn curve runs at its instant; a resting keyframe's head
// is the linked head at its instant, exactly as a stop's.
TabKeyframeLayout tabKeyframeLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe, const double drawn_end,
    const bool end_head_in_front)
{
    // The mark's own instant, on the string line: where a head stands, and the column a dot rides.
    TabKeyframeLayout at_instant;
    at_instant.center_x = geometry.x(std::min(keyframe.seconds, drawn_end));
    at_instant.center_y = geometry.laneY(note.string);
    at_instant.head_size = geometry.headSize();
    TabKeyframeLayout keyframe_layout = std::visit(
        common::core::Overloaded{
            [&](const common::core::KeyframeStopMark& stop) {
                return tabSlideStopLayout(geometry, note, stop.stop, drawn_end);
            },
            [&](const common::core::KeyframeCurveMark&) {
                TabKeyframeLayout layout = at_instant;
                layout.center_y =
                    bendCurveYAt(geometry, layout.center_y, note.bend, keyframe.seconds);
                layout.shape = TabKeyframeShape::Dot;
                layout.box =
                    centeredSquare(layout.center_x, layout.center_y, layout.head_size / 2.0f);
                // A dot reaching into the square of the head its ring ends on would sit on that
                // head's digit while the head stands in front, so there the point's chip is its
                // only face (nextHeadLeftEdge); once the head steps back the dot draws in truth.
                const std::optional<float> head_left = nextHeadLeftEdge(geometry, note);
                layout.mark_drawn = common::core::instantDrawn(keyframe.seconds, drawn_end) &&
                                    !(end_head_in_front && head_left.has_value() &&
                                      layout.box.x + layout.box.width > *head_left);
                return layout;
            },
            [&](const common::core::KeyframeRestMark&) {
                TabKeyframeLayout layout = at_instant;
                layout.mark_drawn = common::core::instantDrawn(keyframe.seconds, drawn_end);
                layout.box = centeredSquare(layout.center_x, layout.center_y, layout.head_size);
                return layout;
            },
        },
        keyframe.mark);
    if (const std::optional<std::size_t> point = keyframe.bend_point; point.has_value())
    {
        keyframe_layout.bend_chip = tabBendPointChipBox(geometry, note, *point, drawn_end);
    }
    return keyframe_layout;
}

} // namespace rock_hero::common::ui
