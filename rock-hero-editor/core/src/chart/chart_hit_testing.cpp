#include "chart/chart_hit_testing.h"

#include <array>
#include <cmath>
#include <initializer_list>
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

// The nearest of one class of targets a press lands on, by horizontal distance to each centre. A
// tie goes to the target considered LATER, the one the lane painted on top.
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
        if (!m_best.has_value() || distance <= m_best_distance)
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

// What one of the paint's layers holds under a press, kept per class as the probe walks the notes.
struct PressLayer
{
    explicit PressLayer(const float press_x) noexcept
        : satellites{press_x}
        , heads{press_x}
    {}

    NearestTarget satellites;
    NearestTarget heads;
    std::optional<ChartHitTarget> top_bend_chip;
    std::optional<ChartHitTarget> top_slide_chip;
    std::optional<ChartHitTarget> top_mark;

    // The target the press reaches in this layer: its satellites, then its heads, then its faces
    // topmost first, as chartHitTarget explains.
    [[nodiscard]] std::optional<ChartHitTarget> topmost() const
    {
        for (const std::optional<ChartHitTarget>* const found :
             {&satellites.best(), &heads.best(), &top_bend_chip, &top_slide_chip, &top_mark})
        {
            if (found->has_value())
            {
                return *found;
            }
        }
        return std::nullopt;
    }
};

} // namespace

std::optional<ChartHitTarget> chartHitTarget(
    const common::core::ChartViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y, const common::ui::TabPresence& presence)
{
    // Everything a press can land on, sorted into the paint's two layers: the notes in front, and
    // beneath them every note STEPPED BACK behind the ring being edited, which the lane draws first
    // and whole, satellite and chips included. The front layer answers first, so a press on the
    // head a focused ring ends on reaches the ring's faces drawn over it.
    std::array<PressLayer, 2> layers{PressLayer{x}, PressLayer{x}};
    const auto layer_of =
        [&layers](const common::ui::TabNotePresence& note_presence) -> PressLayer& {
        return layers.at(note_presence.receded() ? std::size_t{1} : std::size_t{0});
    };

    // The held-stop satellites. A satellite's digit sits at its note's own instant, or at the
    // bracket its tap fronts, and its column lies OUTBOARD of the head's own columns and belongs to
    // no head, so each layer answers it before its heads, keeping it a target of its own instead
    // of letting the ordinary head pass decide the columns beside a head it does not cover. The
    // whole stream is probed rather than culled through the visible range, because a fronted
    // bracket sits at its SPAN's mark, which can be earlier than the note's own instant and
    // therefore outside a window keyed by note ends. Every note that draws no satellite lays out to
    // nothing here and is skipped for free — nothing undrawn is clickable.
    //
    // A REVEAL-ONLY satellite is reachable exactly while it is drawn, which is what handing the
    // reveal to the layout buys: one rectangle answers "is it there" for the painter and for this
    // probe, so the drawn digit and the clickable one cannot part.
    for (std::size_t index = 0; index < tab.notes.size(); ++index)
    {
        const common::ui::TabNotePresence note_presence = common::ui::tabPresence(presence, index);
        const std::optional<common::ui::TabHeldStopLayout> layout =
            common::ui::tabHeldStopLayout(geometry, tab.notes[index], note_presence.revealing());
        if (layout.has_value() && layout->box.contains(x, y))
        {
            layer_of(note_presence)
                .satellites.consider(layout->center_x, ChartHeldStopHit{.index = index});
        }
    }

    // One pass lays every candidate out once and keeps the nearest of each class, and the classes
    // answer in turn (PressLayer::topmost). HEADS first: a note is addressed at its onset column,
    // and its head is the target a charter reaches for most. Every other FACE next — a keyframe's
    // mark, drawn ON a tail (so resolving tails first would make every one unclickable), and the
    // chips, each a face of what owns it: the onset's bend chip reaches its note, a keyframe's bend
    // chip its keyframe. A chip's box is as wide as the widest amount it can print, since this
    // resolver measures no text, so letting chips answer before heads would hand a short chip's
    // empty margin a press meant for the head beside it. Among the faces the one PAINTED ON TOP
    // answers, not the nearest: chips pushed back to one place stack, and boxes of different widths
    // ending at one edge have different centres. So each of the paint core's three layers keeps
    // the last face containing the press, in the paint core's order, and the layers answer topmost
    // first: every bend chip, then every slide chip, then the marks drawn in the note pass.
    const auto [first, last] = candidateRange(tab, geometry, x, x);
    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::NoteViewState& note = tab.notes[index];
        const common::ui::TabNotePresence note_presence = common::ui::tabPresence(presence, index);
        PressLayer& layer = layer_of(note_presence);
        const common::ui::TabNoteLayout note_layout = common::ui::tabNoteLayout(geometry, note);
        if (note_layout.head.contains(x, y))
        {
            layer.heads.consider(note_layout.onset_x, ChartNoteHit{.index = index});
        }
        if (const std::optional<common::ui::TabLayoutRect>& chip = note_layout.bend_chip;
            chip.has_value() && chip->contains(x, y))
        {
            layer.top_bend_chip = ChartNoteHit{.index = index};
        }
        // A mark is clickable exactly where the lane draws it, by the rules the paint core draws
        // by: within the extent the note is drawn to, and past it only as the destination chip a
        // cut leg wears at the crop, which names the keyframe it heads for and so reaches it.
        const double drawn_end = common::ui::drawnExtentSeconds(note, note_presence.reveal);
        const bool end_head_in_front = common::ui::tabEndHeadInFront(presence, note);
        for (std::size_t keyframe = 0; keyframe < note.keyframes.size(); ++keyframe)
        {
            const common::ui::TabKeyframeLayout keyframe_layout = common::ui::tabKeyframeLayout(
                geometry, note, note.keyframes[keyframe], drawn_end, end_head_in_front);
            const ChartKeyframeHit target{.note_index = index, .keyframe_index = keyframe};
            if (keyframe_layout.mark_drawn && keyframe_layout.box.contains(x, y))
            {
                (keyframe_layout.shape == common::ui::TabKeyframeShape::Chip ? layer.top_slide_chip
                                                                             : layer.top_mark) =
                    target;
            }
            if (const std::optional<common::ui::TabLayoutRect>& chip = keyframe_layout.bend_chip;
                chip.has_value() && chip->contains(x, y))
            {
                layer.top_bend_chip = target;
            }
        }
    }
    for (const PressLayer& layer : layers)
    {
        if (std::optional<ChartHitTarget> target = layer.topmost(); target.has_value())
        {
            return target;
        }
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
    float left, float top, float right, float bottom, const common::ui::TabPresence& presence)
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
            common::ui::drawnExtentSeconds(note, common::ui::tabPresence(presence, index).reveal);
        const bool end_head_in_front = common::ui::tabEndHeadInFront(presence, note);
        for (std::size_t keyframe = 0; keyframe < note.keyframes.size(); ++keyframe)
        {
            const common::ui::TabKeyframeLayout layout = common::ui::tabKeyframeLayout(
                geometry, note, note.keyframes[keyframe], drawn_end, end_head_in_front);
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
