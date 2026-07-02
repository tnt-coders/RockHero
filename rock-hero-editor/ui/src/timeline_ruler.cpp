#include "timeline_ruler.h"

#include "editor_colors.h"
#include "timeline_cursor.h"

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

const juce::Colour g_timeline_ruler_color{juce::Colours::darkgrey.darker(0.45f)};
const juce::Colour g_timeline_ruler_text_color{210, 210, 210};
const juce::Colour g_timeline_anchor_color{180, 218, 255};

// Measures text without using JUCE's deprecated Font string-width helpers.
[[nodiscard]] int textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(arrangement.getBoundingBox(0, -1, true).getWidth()));
}

} // namespace

// Names the component for tests and enables direct mouse placement.
TimelineRuler::TimelineRuler()
{
    setComponentID("timeline_ruler");
    setInterceptsMouseClicks(true, false);
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
    refreshGridLines();
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

    repaintCursorStrip(*this, m_cursor_x, next_cursor_x);
    m_cursor_x = next_cursor_x;
}

// Stores the tempo map that supplies measures and anchors, plus the grid step in beats shared
// with the track grid and snapping.
void TimelineRuler::setGrid(
    common::core::TempoMap tempo_map, common::core::Fraction grid_spacing_beats)
{
    if (m_tempo_map == tempo_map && m_grid_spacing_beats == grid_spacing_beats)
    {
        return;
    }

    m_tempo_map = std::move(tempo_map);
    m_grid_spacing_beats = grid_spacing_beats;
    refreshGridLines();
    repaint();
}

// Stores the callback that receives cursor-placement seek positions.
void TimelineRuler::setCursorPlacementCallback(CursorPlacementCallback callback)
{
    m_cursor_placement_callback = std::move(callback);
}

// Paints quiet measure orientation marks and brighter tempo-map anchors.
void TimelineRuler::paint(juce::Graphics& g)
{
    g.fillAll(g_timeline_ruler_color);
    g.setColour(g_track_viewport_color);
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

// Refreshes cached grid-line geometry after a resize changes the visible ruler width.
void TimelineRuler::resized()
{
    refreshGridLines();
}

// Converts ruler clicks into timeline seek positions using scrollable timeline coordinates.
void TimelineRuler::mouseDown(const juce::MouseEvent& event)
{
    if (!m_project_loaded || m_content_width <= 0 || !m_cursor_placement_callback ||
        !event.mods.isLeftButtonDown())
    {
        return;
    }

    const float timeline_x = static_cast<float>(m_view_x) + event.position.x;
    const std::optional<common::core::TimePosition> position = timelineCursorPlacementTime(
        m_tempo_map,
        m_grid_spacing_beats,
        m_timeline_range,
        m_content_width,
        timeline_x,
        event.mods.isCtrlDown() ? TimelineCursorPlacementMode::Free
                                : TimelineCursorPlacementMode::SnapToGrid);
    if (position.has_value())
    {
        m_cursor_placement_callback(*position);
    }
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

// Recomputes cached tick rectangles and measure-label draw positions from the current timeline
// geometry and tempo map. Kept out of paint() so repaints driven only by cursor movement, whether
// vblank-driven playback or a single click, do not repeat the beat scan or the per-label
// GlyphArrangement text-width measurement on every frame; both only need to rerun when the
// geometry or tempo map they depend on actually changes.
void TimelineRuler::refreshGridLines()
{
    const int visible_x_begin = std::max(0, m_view_x);
    const int visible_x_end = std::min(m_content_width, m_view_x + getWidth());
    const std::vector<core::TempoGridLine> lines = core::visibleTempoGridLines(
        m_tempo_map,
        m_grid_spacing_beats,
        m_timeline_range,
        m_content_width,
        visible_x_begin,
        visible_x_end);

    m_tick_rects.clear();
    m_measure_labels.clear();

    const juce::Font font{juce::FontOptions{11.0f}};
    int next_label_x = 4;
    const int beat_tick_height = std::max(1, getHeight() / 4);
    // Subdivision ticks stay half the beat height so the ruler reads which short ticks are real
    // beats even when a fine grid fills the space between them.
    const int subdivision_tick_height = std::max(1, getHeight() / 8);
    for (const core::TempoGridLine& line : lines)
    {
        const int x = line.x - m_view_x;
        if (line.rank != core::TempoGridLineRank::Measure)
        {
            const int tick_height = line.rank == core::TempoGridLineRank::Beat
                                        ? beat_tick_height
                                        : subdivision_tick_height;
            m_tick_rects.addWithoutMerging(
                juce::Rectangle<int>{x, getHeight() - tick_height, 1, tick_height}.toFloat());
            continue;
        }

        m_tick_rects.addWithoutMerging(juce::Rectangle<int>{x, 0, 1, getHeight()}.toFloat());

        const juce::String label{line.measure};
        const int label_width = textWidth(font, label) + 8;
        if (x >= next_label_x && x + label_width <= getWidth())
        {
            m_measure_labels.push_back(MeasureLabel{.x = x, .text = label, .width = label_width});
            next_label_x = x + label_width + 10;
        }
    }
}

// Draws visible beat ticks, with measure ticks promoted to the full ruler height.
void TimelineRuler::drawBeatTicks(juce::Graphics& g)
{
    if (!m_tick_rects.isEmpty())
    {
        g.setColour(g_measure_grid_color);
        g.fillRectList(m_tick_rects);
    }

    g.setFont(juce::FontOptions{11.0f});
    g.setColour(g_timeline_ruler_text_color.withAlpha(0.82f));
    for (const MeasureLabel& label : m_measure_labels)
    {
        g.drawText(label.text, label.x + 4, 2, label.width, 14, juce::Justification::centredLeft);
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

        g.setColour(g_timeline_anchor_color);
        g.fillPath(marker);

        const juce::String label = juce::String{anchor.measure} + ":" + juce::String{anchor.beat} +
                                   "  " + juce::String{anchor.seconds, 3} + "s";
        const int label_x = static_cast<int>(std::round(x)) + 7;
        const int label_width = textWidth(g.getCurrentFont(), label) + 10;
        if (label_x >= next_label_x && label_x + label_width <= getWidth())
        {
            g.setColour(g_timeline_anchor_color);
            g.drawText(label, label_x, 17, label_width, 13, juce::Justification::centredLeft);
            next_label_x = label_x + label_width + 12;
        }
    }
}

// Draws the same transport cursor through the ruler for vertical alignment.
void TimelineRuler::drawCursor(juce::Graphics& g)
{
    if (!m_cursor_x.has_value() || getWidth() <= 0)
    {
        return;
    }

    const int cursor_x = std::clamp(static_cast<int>(std::round(*m_cursor_x)), 0, getWidth() - 1);
    g.setColour(juce::Colours::white);
    g.fillRect(cursor_x, 0, 1, getHeight());
}

} // namespace rock_hero::editor::ui
