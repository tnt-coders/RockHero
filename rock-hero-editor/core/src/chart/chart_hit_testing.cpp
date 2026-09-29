#include "chart/chart_hit_testing.h"

#include <cmath>
#include <optional>
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
    return common::core::visibleEventRange(
        tab.notes, tab.ring_end_prefix_max, span_start, span_end);
}

// The nearest of one class of targets a press lands on, by horizontal distance to each centre.
class NearestTarget
{
public:
    explicit NearestTarget(const float press_x) noexcept
        : m_press_x{press_x}
    {}

    // Keeps the target when it is nearer the press than any kept before it.
    void consider(const float center_x, const ChartHitTarget& target)
    {
        const float distance = std::abs(m_press_x - center_x);
        if (!m_best.has_value() || distance < m_best_distance)
        {
            m_best = target;
            m_best_distance = distance;
        }
    }

    // The nearest target considered, or nothing when none was.
    [[nodiscard]] const std::optional<ChartHitTarget>& best() const noexcept
    {
        return m_best;
    }

private:
    float m_press_x;
    std::optional<ChartHitTarget> m_best;
    float m_best_distance{0.0f};
};

} // namespace

std::optional<ChartHitTarget> chartHitTarget(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y, const common::ui::TabRevealed& revealed)
{
    // The held-stop satellites first. A satellite's digit sits at its note's own instant, or at the
    // bracket its tap fronts, and its column lies OUTBOARD of the head's own columns and belongs to
    // no head, so resolving it here keeps it a target of its own instead of letting the ordinary
    // head pass decide the columns beside a head it does not cover. The whole stream is probed
    // rather than culled through the visible range, because a fronted bracket sits at its SPAN's
    // mark, which can be earlier than the note's own instant and therefore outside a window keyed
    // by note ends. Every note that draws no satellite lays out to nothing here and is skipped for
    // free — nothing undrawn is clickable.
    //
    // A REVEAL-ONLY satellite is reachable exactly while it is drawn, which is what handing the
    // reveal to the layout buys: one rectangle answers "is it there" for the painter and for this
    // probe, so the drawn digit and the clickable one cannot part.
    std::optional<std::size_t> best_satellite;
    float best_satellite_distance = 0.0f;
    for (std::size_t index = 0; index < tab.notes.size(); ++index)
    {
        const std::optional<common::ui::TabHeldStopLayout> layout = common::ui::tabHeldStopLayout(
            geometry, tab.notes[index], common::ui::tabRevealed(revealed, index));
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

    // One pass lays every candidate out once and keeps the nearest of each class, and the classes
    // answer in turn. HEADS first: a note is addressed at its onset column, and its head is the
    // target a charter reaches for most. Every other FACE next — a keyframe's mark, drawn ON a tail
    // (so resolving tails first would make every one unclickable), and the chips, each a face of
    // what owns it: the onset's bend chip reaches its note, a keyframe's bend chip its keyframe.
    // A chip's box is as wide as the widest amount it can print, since this resolver measures no
    // text, so letting chips answer before heads would hand a short chip's empty margin a press
    // meant for the head beside it.
    NearestTarget heads{x};
    NearestTarget faces{x};
    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::NoteViewState& note = tab.notes[index];
        const common::ui::TabNoteLayout layout = common::ui::tabNoteLayout(geometry, note);
        if (layout.head.contains(x, y))
        {
            heads.consider(layout.onset_x, ChartNoteHit{.index = index});
        }
        if (const std::optional<common::ui::TabLayoutRect>& chip = layout.bend_chip;
            chip.has_value() && chip->contains(x, y))
        {
            faces.consider(chip->x + chip->width / 2.0f, ChartNoteHit{.index = index});
        }
        // A mark is clickable exactly where the lane draws it, by the rules the paint core draws
        // by: within the extent the note is drawn to, and past it only as the destination chip a
        // cut leg wears at the crop, which names the keyframe it heads for and so reaches it.
        const double drawn_end =
            common::core::drawnEndSeconds(note, common::ui::tabRevealed(revealed, index));
        for (std::size_t keyframe = 0; keyframe < note.keyframes.size(); ++keyframe)
        {
            const common::ui::TabKeyframeLayout keyframe_layout =
                common::ui::tabKeyframeLayout(geometry, note, note.keyframes[keyframe], drawn_end);
            const ChartKeyframeHit target{.note_index = index, .keyframe_index = keyframe};
            if (keyframe_layout.mark_drawn && keyframe_layout.box.contains(x, y))
            {
                faces.consider(keyframe_layout.center_x, target);
            }
            if (const std::optional<common::ui::TabLayoutRect>& chip = keyframe_layout.bend_chip;
                chip.has_value() && chip->contains(x, y))
            {
                faces.consider(chip->x + chip->width / 2.0f, target);
            }
        }
    }
    if (heads.best().has_value())
    {
        return heads.best();
    }
    if (faces.best().has_value())
    {
        return faces.best();
    }

    // AND NOTHING ELSE. A tail is not a target: a note is addressed at the one column where it
    // happens, and a tail says how long a string rings — testimony, not a handle. Resolving a
    // mid-tail point to the note whose onset is nearest would put the selection somewhere the
    // caret is not; a click on a tail falls through to the ordinary empty-slot placement instead,
    // doing what clicks in this lane always do. Uniformly, too: a VISIBLE tail selects no more
    // than ink a covering span already owns.
    //
    // What answers "is something here?" is the lane reveal, which shows every ring at once for as
    // long as it is held, so the honest answer arrives without the click meaning two things.
    return std::nullopt;
}

std::vector<ChartHitTarget> chartTargetsInBox(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry,
    float left, float top, float right, float bottom, const common::ui::TabRevealed& revealed)
{
    const auto intersects = [left, top, right, bottom](const common::ui::TabLayoutRect& box) {
        return box.x < right && box.x + box.width > left && box.y < bottom &&
               box.y + box.height > top;
    };

    const auto [first, last] = candidateRange(tab, geometry, left, right);
    std::vector<ChartHitTarget> boxed;
    for (std::size_t index = first; index < last; ++index)
    {
        // Either face reaches the note: its head, or the chip printing its onset's bend.
        const common::ui::TabNoteLayout layout =
            common::ui::tabNoteLayout(geometry, tab.notes[index]);
        const std::optional<common::ui::TabLayoutRect>& chip = layout.bend_chip;
        if (intersects(layout.head) || (chip.has_value() && intersects(*chip)))
        {
            boxed.emplace_back(ChartNoteHit{.index = index});
        }
    }
    // The keyframe marks a box catches, on the same drawn rule as a click: a box drawn over a
    // glide's junction selects that junction, which is what makes the marquee reach the objects
    // the click reaches.
    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::NoteViewState& note = tab.notes[index];
        const double drawn_end =
            common::core::drawnEndSeconds(note, common::ui::tabRevealed(revealed, index));
        for (std::size_t keyframe = 0; keyframe < note.keyframes.size(); ++keyframe)
        {
            const common::ui::TabKeyframeLayout layout =
                common::ui::tabKeyframeLayout(geometry, note, note.keyframes[keyframe], drawn_end);
            const std::optional<common::ui::TabLayoutRect>& chip = layout.bend_chip;
            if ((layout.mark_drawn && intersects(layout.box)) ||
                (chip.has_value() && intersects(*chip)))
            {
                boxed.emplace_back(
                    ChartKeyframeHit{.note_index = index, .keyframe_index = keyframe});
            }
        }
    }
    return boxed;
}

} // namespace rock_hero::editor::core
