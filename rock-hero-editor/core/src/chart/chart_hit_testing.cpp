#include "chart/chart_hit_testing.h"

#include <cmath>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Candidate index window for a lane-local x, widened by the head slack the paint core uses so
// heads whose centers sit just outside the probed instant are still candidates.
[[nodiscard]] std::pair<std::size_t, std::size_t> candidateRange(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left_x, float right_x)
{
    const double duration = geometry.visible_timeline.duration().seconds;
    const double seconds_per_pixel = duration / static_cast<double>(geometry.bounds_width);
    const double slack_seconds =
        static_cast<double>(geometry.max_note_height) * 3.0 * seconds_per_pixel;
    const double span_start = geometry.visible_timeline.start.seconds +
                              static_cast<double>(left_x) * seconds_per_pixel - slack_seconds;
    const double span_end = geometry.visible_timeline.start.seconds +
                            static_cast<double>(right_x) * seconds_per_pixel + slack_seconds;
    // The prefix table is rebuilt per query: hit resolution runs once per pointer event, not per
    // frame, and the controller does not retain a per-projection index the way the lane view
    // does for painting. Built from the notes' own presented ends, exactly as the paint core's
    // index is, so the candidate window covers what the lane drew and no more.
    const std::vector<double> prefix = common::core::makeSustainPrefixMax(tab.notes);
    return common::core::visibleEventRange(tab.notes, prefix, span_start, span_end);
}

// True when the lane draws a head at this keyframe, which is exactly when it is clickable: an
// unlinked keyframe sits at the presented tail's end, where the re-picked landing draws its own
// head instead. The same read the paint core gates its linked-head passes on.
[[nodiscard]] bool keyframeHasHead(
    const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe) noexcept
{
    return common::core::linkedKeyframe(note, keyframe);
}

} // namespace

std::optional<ChartHitTarget> chartHitTarget(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y, const ChartNoteRevealed& revealed)
{
    // Silently-held stops first, which is the ONE place this order departs from "topmost drawn
    // wins", so the reason is stated rather than inferred from the position. A hold's face is the
    // arpeggio bracket printing its stop, and no FRETTING-HAND head of its string is drawn under
    // that bracket anywhere: a claim only ever gets a face on a string the span's own sound never
    // states, and the growth law splits the span at any fretting-hand stop the shape does not
    // state, so every fretting-hand sounding inside the span is on some OTHER string. The warrant
    // used to be narrower — slot uniqueness at the span's own start — which stopped covering the
    // case once the bracket learned to defer to an interior sounding ([D2] amendment 2); this one
    // holds wherever the mark lands. What it CAN overlap is a head slightly later on the same
    // string, and the paint core draws brackets before heads, so that head is on top. The hold
    // takes the overlap anyway: the bracket is its ONLY affordance, and yielding leaves it a
    // two-pixel bar, while the head keeps every column the bracket does not reach. The trade is
    // recorded with the verb's design record (`docs/plans/todo/arpeggio-authoring.md`) rather than
    // settled silently here.
    //
    // The one head the argument above does not cover is a RIGHT-HAND onset's: a tap joins no
    // posture, so it can sound the hold's own string inside the span without splitting it, and a
    // deferred bracket can land on the slot it shares. Recorded as a sighting item rather than
    // arbitrated blind (`docs/tracking/watch-items.md`) — the figure needs eyes before a priority
    // is chosen for it.
    //
    // The whole stream is probed rather than culled through the visible range, because a hold's
    // bracket sits at its SPAN's mark, which can be earlier than the hold's own instant and
    // therefore outside a window keyed by note ends. Every note that is not a resolved hold lays
    // out to nothing here and is skipped for free — nothing undrawn is clickable.
    std::optional<std::size_t> best_hold;
    float best_hold_distance = 0.0f;
    for (std::size_t index = 0; index < tab.notes.size(); ++index)
    {
        const std::optional<common::ui::TabSilentHoldLayout> layout =
            common::ui::tabSilentHoldLayout(geometry, tab.notes[index]);
        if (!layout.has_value() || !layout->box.contains(x, y))
        {
            continue;
        }
        const float distance = std::abs(x - layout->center_x);
        if (!best_hold.has_value() || distance < best_hold_distance)
        {
            best_hold = index;
            best_hold_distance = distance;
        }
    }
    if (best_hold.has_value())
    {
        return ChartNoteHit{.index = *best_hold};
    }

    // The held-stop satellites, on exactly the reasoning above and with the same whole-stream
    // probe: the digit sits at its note's own instant, or at the bracket its tap fronts, so a
    // window keyed by note ends would still cover it — but the column lies OUTBOARD of the head's
    // own columns and belongs to no head, so resolving it here keeps it a target of its own instead
    // of letting the ordinary head pass decide the columns beside a head it does not cover.
    //
    // A REVEAL-ONLY satellite is reachable exactly while it is drawn, which is what handing the
    // reveal to the layout buys: one rectangle answers "is it there" for the painter and for this
    // probe, so the drawn digit and the clickable one cannot part.
    std::optional<std::size_t> best_satellite;
    float best_satellite_distance = 0.0f;
    for (std::size_t index = 0; index < tab.notes.size(); ++index)
    {
        const std::optional<common::ui::TabHeldStopLayout> layout =
            common::ui::tabHeldStopLayout(geometry, tab.notes[index], revealed && revealed(index));
        if (!layout.has_value() || !layout->box.contains(x, y))
        {
            continue;
        }
        const float distance = std::abs(x - layout->center_x);
        if (!best_satellite.has_value() || distance < best_satellite_distance)
        {
            best_satellite = index;
            best_satellite_distance = distance;
        }
    }
    if (best_satellite.has_value())
    {
        return ChartHeldStopHit{.index = *best_satellite};
    }

    const auto [first, last] = candidateRange(tab, geometry, x, x);

    // Heads next, and heads are the LAST note target: a note is addressed at its onset column and
    // nowhere else. Among overlapping heads the nearest onset center wins.
    std::optional<std::size_t> best_head;
    float best_head_distance = 0.0f;
    for (std::size_t index = first; index < last; ++index)
    {
        // A silent hold draws no head and no tail, so nothing of it is clickable at its own
        // instant; its face was resolved above, wherever its span's mark draws. Skipped rather
        // than let the note layout hand back a rectangle the lane never painted.
        if (common::core::silentHold(tab.notes[index].attack))
        {
            continue;
        }
        const common::ui::TabNoteLayout layout =
            common::ui::tabNoteLayout(geometry, tab.notes[index]);
        if (!layout.head.contains(x, y))
        {
            continue;
        }
        const float distance = std::abs(x - layout.onset_x);
        if (!best_head.has_value() || distance < best_head_distance)
        {
            best_head = index;
            best_head_distance = distance;
        }
    }
    if (best_head.has_value())
    {
        return ChartNoteHit{.index = *best_head};
    }

    // Linked keyframe heads next: they are drawn ON a tail, so resolving tails first would make
    // every one of them unclickable. Nearest head center wins among overlapping ones, the same
    // rule the onset heads use.
    std::optional<ChartKeyframeHit> best_keyframe;
    float best_keyframe_distance = 0.0f;
    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::NoteViewState& note = tab.notes[index];
        for (std::size_t keyframe = 0; keyframe < note.slides.size(); ++keyframe)
        {
            if (!keyframeHasHead(note, note.slides[keyframe]))
            {
                continue;
            }
            const common::ui::TabKeyframeLayout layout =
                common::ui::tabKeyframeLayout(geometry, note, note.slides[keyframe]);
            if (!layout.head.contains(x, y))
            {
                continue;
            }
            const float distance = std::abs(x - layout.center_x);
            if (!best_keyframe.has_value() || distance < best_keyframe_distance)
            {
                best_keyframe = ChartKeyframeHit{.note_index = index, .keyframe_index = keyframe};
                best_keyframe_distance = distance;
            }
        }
    }
    if (best_keyframe.has_value())
    {
        return *best_keyframe;
    }

    // AND NOTHING ELSE. A tail is not a target (user ruling 2026-08-30): a note is addressed at the
    // one column where it happens, and a tail says how long a string rings — testimony, not a
    // handle. The pass that stood here resolved a mid-tail point to the note whose onset was
    // nearest, which put the selection somewhere the caret was not; a click on a tail now falls
    // through to the ordinary empty-slot placement, doing what clicks in this lane always do.
    // Uniformly, too: a VISIBLE tail selects no more than ink a covering span already owns.
    //
    // What answers "is something here?" is the caret's own peek — the lane reveals the ring it
    // sits inside — so the honest answer arrives without the click meaning two things.
    return std::nullopt;
}

std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom)
{
    const auto intersects = [left, top, right, bottom](const common::ui::TabLayoutRect& box) {
        return box.x < right && box.x + box.width > left && box.y < bottom &&
               box.y + box.height > top;
    };

    const auto [first, last] = candidateRange(tab, geometry, left, right);
    std::vector<ChartHitTarget> boxed;
    for (std::size_t index = first; index < last; ++index)
    {
        if (common::core::silentHold(tab.notes[index].attack))
        {
            // Its face is the bracket, boxed by the pass below; it draws no head to catch here.
            continue;
        }
        const common::ui::TabNoteLayout layout =
            common::ui::tabNoteLayout(geometry, tab.notes[index]);
        if (intersects(layout.head))
        {
            boxed.push_back(ChartNoteHit{.index = index});
        }
    }
    // The silent holds a box catches, over the whole stream for the same reason the click probe
    // uses: a hold's bracket can sit earlier than the window the note range covers.
    for (std::size_t index = 0; index < tab.notes.size(); ++index)
    {
        const std::optional<common::ui::TabSilentHoldLayout> layout =
            common::ui::tabSilentHoldLayout(geometry, tab.notes[index]);
        if (layout.has_value() && intersects(layout->box))
        {
            boxed.push_back(ChartNoteHit{.index = index});
        }
    }
    // The keyframe heads a box catches, on the same drawn-extent rule as the two above: a box
    // drawn over a glide's junction selects that junction, which is what makes the marquee reach
    // the objects the click reaches.
    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::NoteViewState& note = tab.notes[index];
        for (std::size_t keyframe = 0; keyframe < note.slides.size(); ++keyframe)
        {
            if (!keyframeHasHead(note, note.slides[keyframe]))
            {
                continue;
            }
            const common::ui::TabKeyframeLayout layout =
                common::ui::tabKeyframeLayout(geometry, note, note.slides[keyframe]);
            if (intersects(layout.head))
            {
                boxed.push_back(ChartKeyframeHit{.note_index = index, .keyframe_index = keyframe});
            }
        }
    }
    return boxed;
}

} // namespace rock_hero::editor::core
