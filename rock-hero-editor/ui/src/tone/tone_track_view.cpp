#include "tone_track_view.h"

#include <algorithm>
#include <optional>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_region_vertical_inset{6};
constexpr int g_region_corner_radius{6};
constexpr int g_region_label_inset{8};
const juce::Colour g_tone_row_divider{juce::Colours::white.withAlpha(0.18f)};
const juce::Colour g_tone_region_fill{juce::Colour{0xff2b4a66}};
const juce::Colour g_tone_region_border{juce::Colours::lightskyblue.withAlpha(0.65f)};
const juce::Colour g_tone_region_label{juce::Colours::white.withAlpha(0.92f)};
// The synthesized legacy-default region reads as a passive continuation, not authored content.
const juce::Colour g_default_region_fill{juce::Colours::white.withAlpha(0.05f)};
const juce::Colour g_default_region_border{juce::Colours::white.withAlpha(0.25f)};
const juce::Colour g_default_region_label{juce::Colours::white.withAlpha(0.55f)};

// Region names may be empty in the data model; the row still labels every region.
[[nodiscard]] juce::String regionLabel(const core::ToneRegionViewState& region)
{
    return region.name.empty() ? juce::String{"Tone"} : juce::String{region.name};
}

} // namespace

ToneTrackView::ToneTrackView()
{
    setInterceptsMouseClicks(false, false);
}

void ToneTrackView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    if (m_visible_timeline == visible_timeline)
    {
        return;
    }

    m_visible_timeline = visible_timeline;
    repaint();
}

void ToneTrackView::setState(const core::ToneTrackViewState& state)
{
    if (m_state == state)
    {
        return;
    }

    m_state = state;
    repaint();
}

void ToneTrackView::paint(juce::Graphics& g)
{
    // The row stays transparent so the canvas-level tempo grid shows through between regions;
    // regions snap to that grid, so hiding it here would remove the alignment cue.
    const auto bounds = getLocalBounds();
    g.setColour(g_tone_row_divider);
    g.drawLine(0.0f, 0.0f, static_cast<float>(bounds.getWidth()), 0.0f, 1.0f);

    for (const core::ToneRegionViewState& region : m_state.regions)
    {
        const std::optional<float> start_x = core::timelineXForPosition(
            region.time_range.start,
            m_visible_timeline,
            bounds.getWidth(),
            core::TimelinePositionClamping::ClampToVisibleRange);
        const std::optional<float> end_x = core::timelineXForPosition(
            region.time_range.end,
            m_visible_timeline,
            bounds.getWidth(),
            core::TimelinePositionClamping::ClampToVisibleRange);
        if (!start_x.has_value() || !end_x.has_value() || *end_x <= *start_x)
        {
            continue;
        }

        const juce::Rectangle<float> region_bounds{
            *start_x,
            static_cast<float>(g_region_vertical_inset),
            *end_x - *start_x,
            static_cast<float>(std::max(1, bounds.getHeight() - (g_region_vertical_inset * 2))),
        };

        const bool is_default = region.synthesized_default;
        g.setColour(is_default ? g_default_region_fill : g_tone_region_fill);
        g.fillRoundedRectangle(region_bounds, static_cast<float>(g_region_corner_radius));
        g.setColour(is_default ? g_default_region_border : g_tone_region_border);
        g.drawRoundedRectangle(region_bounds, static_cast<float>(g_region_corner_radius), 1.2f);

        const auto label_bounds =
            region_bounds.reduced(static_cast<float>(g_region_label_inset), 0.0f).toNearestInt();
        if (label_bounds.getWidth() > 0)
        {
            g.setColour(is_default ? g_default_region_label : g_tone_region_label);
            g.setFont(juce::FontOptions{13.0f});
            g.drawText(regionLabel(region), label_bounds, juce::Justification::centredLeft, true);
        }
    }
}

} // namespace rock_hero::editor::ui
