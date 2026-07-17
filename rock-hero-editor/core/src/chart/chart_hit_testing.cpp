#include "chart/chart_hit_testing.h"

#include <cmath>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Candidate index window for a lane-local x, widened by the head slack the paint core uses so
// heads whose centers sit just outside the probed instant are still candidates.
[[nodiscard]] std::pair<std::size_t, std::size_t> candidateRange(
    const common::core::TabViewState& tab, const common::ui::TabLaneGeometry& geometry,
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
    // does for painting.
    const std::vector<double> prefix = common::ui::tabPrefixMaxEndSeconds(tab.notes);
    return common::ui::tabVisibleNoteRange(tab.notes, prefix, span_start, span_end);
}

} // namespace

std::optional<std::size_t> chartNoteHitIndex(
    const common::core::TabViewState& tab, const common::ui::TabLaneGeometry& geometry, float x,
    float y)
{
    const auto [first, last] = candidateRange(tab, geometry, x, x);

    // Heads first: the head is the note's primary affordance, so one sitting on another note's
    // tail must win the click. Among overlapping heads the nearest onset center wins.
    std::optional<std::size_t> best_head;
    float best_head_distance = 0.0f;
    for (std::size_t index = first; index < last; ++index)
    {
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
        return best_head;
    }

    // Tails second: overlapping same-string sustains resolve to the nearest onset so the click
    // lands on the note whose tail most plausibly owns the probed span.
    std::optional<std::size_t> best_tail;
    float best_tail_distance = 0.0f;
    for (std::size_t index = first; index < last; ++index)
    {
        const common::ui::TabNoteLayout layout =
            common::ui::tabNoteLayout(geometry, tab.notes[index]);
        if (layout.tail.width <= 0.0f || !layout.tail.contains(x, y))
        {
            continue;
        }
        const float distance = std::abs(x - layout.onset_x);
        if (!best_tail.has_value() || distance < best_tail_distance)
        {
            best_tail = index;
            best_tail_distance = distance;
        }
    }
    return best_tail;
}

std::vector<std::size_t> chartNoteIndicesInBox(
    const common::core::TabViewState& tab, const common::ui::TabLaneGeometry& geometry, float left,
    float top, float right, float bottom)
{
    const auto [first, last] = candidateRange(tab, geometry, left, right);

    std::vector<std::size_t> boxed;
    for (std::size_t index = first; index < last; ++index)
    {
        const common::ui::TabNoteLayout layout =
            common::ui::tabNoteLayout(geometry, tab.notes[index]);
        const bool intersects = layout.head.x < right && layout.head.x + layout.head.width > left &&
                                layout.head.y < bottom && layout.head.y + layout.head.height > top;
        if (intersects)
        {
            boxed.push_back(index);
        }
    }
    return boxed;
}

} // namespace rock_hero::editor::core
