#include "tab/tab_view.h"

#include <memory>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

// The notation rasterizer lives in the shared paint core (rock-hero-common/ui tab/), one
// authority for the editor lane and the game tab strips; these free functions stay on the
// editor surface as thin delegates so editor widgets and tests keep their existing seam.

// The chart's string count floors the lane count so a user minimum can only add empty lanes.
int tabDisplayedStringCount(int chart_string_count, int minimum_displayed_strings) noexcept
{
    return common::ui::tabDisplayedStringCount(chart_string_count, minimum_displayed_strings);
}

juce::Colour tabStringColor(int displayed_string, int displayed_string_count)
{
    return common::ui::tabStringColor(displayed_string, displayed_string_count);
}

juce::Colour tabShapeMarkColor(bool arpeggio)
{
    return common::ui::tabShapeMarkColor(arpeggio);
}

float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept
{
    return common::ui::tabLaneCenterY(displayed_string, displayed_string_count, bounds);
}

std::pair<std::size_t, std::size_t> tabVisibleNoteRange(
    const std::vector<common::core::TabNoteView>& notes,
    const std::vector<double>& prefix_max_end_seconds, double span_start_seconds,
    double span_end_seconds) noexcept
{
    return common::ui::tabVisibleNoteRange(
        notes, prefix_max_end_seconds, span_start_seconds, span_end_seconds);
}

// Pointer events fall through to the cursor overlay's click-to-seek handling.
TabView::TabView()
{
    setInterceptsMouseClicks(false, false);
}

// Stores the visible timeline range used to map note times to pixels.
void TabView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    if (m_visible_timeline == visible_timeline)
    {
        return;
    }

    m_visible_timeline = visible_timeline;
    repaint();
}

// Applies the current tab projection and lane-count preference; the projection pointer only
// changes when the displayed arrangement changes, so pointer identity gates the index rebuild.
void TabView::setState(
    std::shared_ptr<const common::core::TabViewState> tab, int minimum_displayed_strings)
{
    const bool tab_changed = tab != m_tab;
    const bool lanes_changed = minimum_displayed_strings != m_minimum_displayed_strings;
    if (!tab_changed && !lanes_changed)
    {
        return;
    }

    m_tab = std::move(tab);
    m_minimum_displayed_strings = minimum_displayed_strings;
    if (tab_changed)
    {
        rebuildVisibilityIndex();
    }

    repaint();
}

// Guards the empty cases, derives the shared metrics, and delegates the drawing to the shared
// notation paint core.
void TabView::paint(juce::Graphics& g)
{
    if (m_tab == nullptr || m_tab->string_count <= 0)
    {
        return;
    }

    const juce::Rectangle<int> bounds = getLocalBounds();
    if (bounds.isEmpty() || m_visible_timeline.duration().seconds <= 0.0)
    {
        return;
    }

    const int displayed_count =
        tabDisplayedStringCount(m_tab->string_count, m_minimum_displayed_strings);
    const common::ui::TabLaneMetrics metrics = common::ui::makeTabLaneMetrics(
        bounds, m_visible_timeline, displayed_count, m_tab->string_count);
    common::ui::paintTabLane(g, metrics, *m_tab, m_prefix_max_end_seconds);
}

// Rebuilds the prefix-maximum sustain-end table after the projection changes.
void TabView::rebuildVisibilityIndex()
{
    m_prefix_max_end_seconds =
        m_tab == nullptr ? std::vector<double>{} : common::ui::tabPrefixMaxEndSeconds(m_tab->notes);
}

} // namespace rock_hero::editor::ui
