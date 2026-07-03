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
const juce::Colour g_timeline_signature_color{255, 214, 140};

// Shared ruler text size; cached label widths are measured with this same font, so the two must
// not diverge.
constexpr float g_ruler_font_height{11.0f};

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

// Stores the ruler geometry derived from the viewport and zoomed content. The rebuild and repaint
// are deferred to the setGridLines push that owning-view callers issue after every view change,
// because tick coordinates need lines scanned for the new span.
void TimelineRuler::setTimelineView(
    common::core::TimeRange timeline_range, int content_width, int view_x)
{
    m_timeline_range = timeline_range;
    m_content_width = content_width;
    m_view_x = view_x;
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

// Stores the tempo map that supplies anchors and click snapping, plus the grid step in beats
// shared with the track grid and snapping.
void TimelineRuler::setGrid(
    common::core::TempoMap tempo_map, common::core::Fraction grid_spacing_beats)
{
    if (m_tempo_map == tempo_map && m_grid_spacing_beats == grid_spacing_beats)
    {
        return;
    }

    m_tempo_map = std::move(tempo_map);
    m_grid_spacing_beats = grid_spacing_beats;
    refreshRulerGeometry();
    repaint();
}

// Stores the shared visible-span grid lines and rebuilds the cached ruler geometry from them.
// This is the one scan result both the ruler and the track content render from.
void TimelineRuler::setGridLines(std::vector<core::TempoGridLine> grid_lines)
{
    m_grid_lines = std::move(grid_lines);
    refreshRulerGeometry();
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

// Refreshes cached ruler geometry after a resize changes the visible ruler width.
void TimelineRuler::resized()
{
    refreshRulerGeometry();
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

// Rebuilds cached tick rectangles, measure-label draw positions, anchor markers, and anchor
// labels from the stored grid lines, timeline geometry, and tempo map. Kept out of paint() so
// repaints driven only by cursor movement, whether vblank-driven playback or a single click, do
// not rebuild geometry or repeat the per-label GlyphArrangement text-width measurement on every
// frame; all of these only need to rerun when the state they depend on actually changes.
void TimelineRuler::refreshRulerGeometry()
{
    m_tick_rects.clear();
    m_measure_labels.clear();

    const juce::Font font{juce::FontOptions{g_ruler_font_height}};
    refreshSignatureLabels(font);

    // Signature labels own their spots in the top band; measure numbers yield to them so a meter
    // change is never hidden by routine numbering.
    const auto overlaps_signature_label = [this](int label_x, int label_width) {
        for (const RulerLabel& signature : m_signature_labels)
        {
            if (label_x < signature.x + signature.width && signature.x < label_x + label_width)
            {
                return true;
            }
        }

        return false;
    };

    int next_label_x = 4;
    const int beat_tick_height = std::max(1, getHeight() / 4);
    // Subdivision ticks stay half the beat height so the ruler reads which short ticks are real
    // beats even when a fine grid fills the space between them.
    const int subdivision_tick_height = std::max(1, getHeight() / 8);
    for (const core::TempoGridLine& line : m_grid_lines)
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
        if (x >= next_label_x && x + label_width <= getWidth() &&
            !overlaps_signature_label(x + 4, label_width))
        {
            // The stored x carries the small inset off the measure tick so drawing needs no
            // per-label offset math.
            m_measure_labels.push_back(RulerLabel{.x = x + 4, .text = label, .width = label_width});
            next_label_x = x + label_width + 10;
        }
    }

    refreshAnchorGeometry(font);
}

// Rebuilds the time-signature labels drawn at each stored signature change's downbeat, including
// the initial signature at measure 1. Positions come from the tempo map rather than the grid
// lines because a coarse or offset grid spacing can leave a change's downbeat without a grid
// line.
void TimelineRuler::refreshSignatureLabels(const juce::Font& font)
{
    m_signature_labels.clear();

    int next_label_x = 4;
    for (const common::core::TimeSignatureChange& signature : m_tempo_map.timeSignatures())
    {
        const auto local_x = localXForSeconds(m_tempo_map.secondsAtBeat(signature.measure, 1));
        if (!local_x.has_value())
        {
            continue;
        }

        const juce::String label =
            juce::String{signature.numerator} + "/" + juce::String{signature.denominator};
        const int label_x = static_cast<int>(std::round(*local_x)) + 4;
        const int label_width = textWidth(font, label) + 8;
        if (label_x >= next_label_x && label_x + label_width <= getWidth())
        {
            m_signature_labels.push_back(
                RulerLabel{.x = label_x, .text = label, .width = label_width});
            next_label_x = label_x + label_width + 10;
        }
    }
}

// Rebuilds the merged anchor-marker path and the overlap-suppressed anchor labels for the current
// view. Runs on the same geometry- and tempo-map-change cadence as the tick cache.
void TimelineRuler::refreshAnchorGeometry(const juce::Font& font)
{
    m_anchor_markers.clear();
    m_anchor_labels.clear();

    int next_label_x = 4;
    for (const common::core::BeatAnchor& anchor : m_tempo_map.anchors())
    {
        const auto local_x = localXForSeconds(anchor.seconds);
        if (!local_x.has_value())
        {
            continue;
        }

        const float x = *local_x;
        m_anchor_markers.startNewSubPath(x, 11.0f);
        m_anchor_markers.lineTo(x + 4.0f, 15.0f);
        m_anchor_markers.lineTo(x, 19.0f);
        m_anchor_markers.lineTo(x - 4.0f, 15.0f);
        m_anchor_markers.closeSubPath();

        const juce::String label = juce::String{anchor.measure} + ":" + juce::String{anchor.beat} +
                                   "  " + juce::String{anchor.seconds, 3} + "s";
        const int label_x = static_cast<int>(std::round(x)) + 7;
        const int label_width = textWidth(font, label) + 10;
        if (label_x >= next_label_x && label_x + label_width <= getWidth())
        {
            m_anchor_labels.push_back(
                RulerLabel{.x = label_x, .text = label, .width = label_width});
            next_label_x = label_x + label_width + 12;
        }
    }
}

// Draws visible beat ticks, with measure ticks promoted to the full ruler height, plus the top
// band's measure numbers and time-signature labels.
void TimelineRuler::drawBeatTicks(juce::Graphics& g)
{
    if (!m_tick_rects.isEmpty())
    {
        g.setColour(g_measure_grid_color);
        g.fillRectList(m_tick_rects);
    }

    g.setColour(g_timeline_ruler_text_color.withAlpha(0.82f));
    drawLabelRow(g, m_measure_labels, 2, 14);

    g.setColour(g_timeline_signature_color);
    drawLabelRow(g, m_signature_labels, 2, 14);
}

// Draws the cached anchor diamonds and labels; all geometry and text measurement happened in
// refreshAnchorGeometry so this stays cheap on cursor-driven repaints.
void TimelineRuler::drawAnchors(juce::Graphics& g)
{
    g.setColour(g_timeline_anchor_color);
    g.fillPath(m_anchor_markers);
    drawLabelRow(g, m_anchor_labels, 17, 13);
}

// Draws one cached row of overlap-suppressed labels in the current colour at a fixed vertical
// band.
void TimelineRuler::drawLabelRow(
    juce::Graphics& g, const std::vector<RulerLabel>& labels, int y, int height)
{
    g.setFont(juce::FontOptions{g_ruler_font_height});
    for (const RulerLabel& label : labels)
    {
        g.drawText(label.text, label.x, y, label.width, height, juce::Justification::centredLeft);
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
