#include "timeline_ruler.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/editor/core/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline_geometry.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

const juce::Colour g_track_viewport_colour{juce::Colours::darkgrey.darker(0.34f)};
const juce::Colour g_measure_grid_colour{108, 108, 108};
const juce::Colour g_timeline_ruler_colour{juce::Colours::darkgrey.darker(0.45f)};
const juce::Colour g_timeline_ruler_text_colour{210, 210, 210};
const juce::Colour g_timeline_anchor_colour{180, 218, 255};

// Measures text without using JUCE's deprecated Font string-width helpers.
[[nodiscard]] int textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(arrangement.getBoundingBox(0, -1, true).getWidth()));
}

} // namespace

// Names the component for tests and keeps it mouse-transparent for now.
TimelineRuler::TimelineRuler()
{
    setComponentID("timeline_ruler");
    setInterceptsMouseClicks(false, false);
}

// Stores whether the ruler should draw musical position data.
void TimelineRuler::setProjectLoaded(bool project_loaded)
{
    if (m_project_loaded == project_loaded)
    {
        return;
    }

    m_project_loaded = project_loaded;
    repaint();
}

// Stores the ruler geometry derived from the viewport and zoomed content.
void TimelineRuler::setTimelineView(
    common::core::TimeRange timeline_range, int content_width, int view_x)
{
    if (m_timeline_range == timeline_range && m_content_width == content_width &&
        m_view_x == view_x)
    {
        return;
    }

    m_timeline_range = timeline_range;
    m_content_width = content_width;
    m_view_x = view_x;
    repaint();
}

// Samples the current transport cursor for the ruler's aligned playhead mark.
void TimelineRuler::setCursorPosition(common::core::TimePosition cursor_position)
{
    const auto next_cursor_x = localXForSeconds(cursor_position.seconds);
    if (next_cursor_x == m_cursor_x)
    {
        return;
    }

    repaintCursorMovement(m_cursor_x, next_cursor_x);
    m_cursor_x = next_cursor_x;
}

// Stores the tempo map that supplies measures and anchors.
void TimelineRuler::setTempoMap(common::core::TempoMap tempo_map)
{
    if (m_tempo_map == tempo_map)
    {
        return;
    }

    m_tempo_map = std::move(tempo_map);
    repaint();
}

// Paints quiet measure orientation marks and brighter tempo-map anchors.
void TimelineRuler::paint(juce::Graphics& g)
{
    g.fillAll(g_timeline_ruler_colour);
    g.setColour(g_track_viewport_colour);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    if (!m_project_loaded || getWidth() <= 0 || m_content_width <= 0 ||
        m_timeline_range.duration().seconds <= 0.0)
    {
        return;
    }

    drawBeatTicks(g);
    drawAnchors(g);
    drawCursor(g);
}

// Maps an absolute timeline second to this pinned ruler's local x coordinate.
std::optional<float> TimelineRuler::localXForSeconds(double seconds) const noexcept
{
    const auto content_x = core::timelineXForPosition(
        common::core::TimePosition{seconds},
        m_timeline_range,
        m_content_width,
        core::TimelinePositionClamping::RejectOutsideVisibleRange);
    if (!content_x.has_value())
    {
        return std::nullopt;
    }

    const float local_x = *content_x - static_cast<float>(m_view_x);
    if (local_x < 0.0f || local_x >= static_cast<float>(getWidth()))
    {
        return std::nullopt;
    }

    return local_x;
}

// Draws visible beat ticks, with measure ticks promoted to the full ruler height.
void TimelineRuler::drawBeatTicks(juce::Graphics& g)
{
    g.setFont(juce::FontOptions{11.0f});
    int next_label_x = 4;
    const int visible_x_begin = std::max(0, m_view_x);
    const int visible_x_end = std::min(m_content_width, m_view_x + getWidth());
    const std::vector<core::TempoGridLine> lines = core::visibleTempoGridLines(
        m_tempo_map, m_timeline_range, m_content_width, visible_x_begin, visible_x_end);

    juce::RectangleList<float> tick_rects;
    tick_rects.ensureStorageAllocated(static_cast<int>(lines.size()));

    const int beat_tick_height = std::max(1, getHeight() / 4);
    const int beat_tick_y = getHeight() - beat_tick_height;
    for (const core::TempoGridLine& line : lines)
    {
        const int x = line.x - m_view_x;
        if (line.measure_start)
        {
            tick_rects.addWithoutMerging(juce::Rectangle<int>{x, 0, 1, getHeight()}.toFloat());
        }
        else
        {
            tick_rects.addWithoutMerging(
                juce::Rectangle<int>{x, beat_tick_y, 1, beat_tick_height}.toFloat());
        }
    }

    if (!tick_rects.isEmpty())
    {
        g.setColour(g_measure_grid_colour);
        g.fillRectList(tick_rects);
    }

    for (const core::TempoGridLine& line : lines)
    {
        if (!line.measure_start)
        {
            continue;
        }

        const int x = line.x - m_view_x;
        const juce::String label{line.measure};
        const int label_width = textWidth(g.getCurrentFont(), label) + 8;
        if (x >= next_label_x && x + label_width <= getWidth())
        {
            g.setColour(g_timeline_ruler_text_colour.withAlpha(0.82f));
            g.drawText(label, x + 4, 2, label_width, 14, juce::Justification::centredLeft);
            next_label_x = x + label_width + 10;
        }
    }
}

// Draws timing anchors as diamonds and labels precise seconds when horizontal room allows.
void TimelineRuler::drawAnchors(juce::Graphics& g)
{
    g.setFont(juce::FontOptions{11.0f});
    int next_label_x = 4;

    for (const common::core::BeatAnchor& anchor : m_tempo_map.anchors())
    {
        const auto local_x = localXForSeconds(anchor.seconds);
        if (!local_x.has_value())
        {
            continue;
        }

        const float x = *local_x;
        juce::Path marker;
        marker.startNewSubPath(x, 11.0f);
        marker.lineTo(x + 4.0f, 15.0f);
        marker.lineTo(x, 19.0f);
        marker.lineTo(x - 4.0f, 15.0f);
        marker.closeSubPath();

        g.setColour(g_timeline_anchor_colour);
        g.fillPath(marker);

        const juce::String label = juce::String{anchor.measure} + ":" + juce::String{anchor.beat} +
                                   "  " + juce::String{anchor.seconds, 3} + "s";
        const int label_x = static_cast<int>(std::round(x)) + 7;
        const int label_width = textWidth(g.getCurrentFont(), label) + 10;
        if (label_x >= next_label_x && label_x + label_width <= getWidth())
        {
            g.setColour(g_timeline_anchor_colour);
            g.drawText(label, label_x, 17, label_width, 13, juce::Justification::centredLeft);
            next_label_x = label_x + label_width + 12;
        }
    }
}

// Draws the same transport cursor through the ruler for vertical alignment.
void TimelineRuler::drawCursor(juce::Graphics& g)
{
    if (!m_cursor_x.has_value())
    {
        return;
    }

    g.setColour(juce::Colours::white);
    g.drawLine(*m_cursor_x, 0.0f, *m_cursor_x, static_cast<float>(getHeight()), 2.0f);
}

// Repaints the old/new ruler cursor strips without redrawing the whole ruler every frame.
void TimelineRuler::repaintCursorMovement(
    std::optional<float> previous_cursor_x, std::optional<float> next_cursor_x)
{
    if ((!previous_cursor_x.has_value() && !next_cursor_x.has_value()) || getWidth() <= 0 ||
        getHeight() <= 0)
    {
        return;
    }

    float left_x = 0.0f;
    float right_x = 0.0f;
    if (previous_cursor_x.has_value() && next_cursor_x.has_value())
    {
        left_x = std::min(*previous_cursor_x, *next_cursor_x);
        right_x = std::max(*previous_cursor_x, *next_cursor_x);
    }
    else
    {
        const float cursor_x = previous_cursor_x.has_value() ? *previous_cursor_x : *next_cursor_x;
        left_x = cursor_x;
        right_x = cursor_x;
    }

    constexpr int padding = 3;
    const int left = std::max(0, static_cast<int>(std::floor(left_x)) - padding);
    const int right = std::min(getWidth(), static_cast<int>(std::ceil(right_x)) + padding + 1);
    repaint(left, 0, right - left, getHeight());
}

} // namespace rock_hero::editor::ui
