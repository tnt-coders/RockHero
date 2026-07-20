#include "cursor_overlay.h"

#include "shared/editor_theme.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <utility>

namespace rock_hero::editor::ui
{

// Starts vblank-driven cursor refresh against the injected read-only transport. The tempo map
// used to snap clicks is referenced (not copied) from the owning view's state, which owns it and
// outlives this overlay, so it is never null.
CursorOverlay::CursorOverlay(
    core::IEditorController& controller, const common::audio::ITransport& transport,
    const common::core::TempoMap& tempo_map)
    : m_controller(controller)
    , m_transport(transport)
    , m_vblank_attachment(this, [this] { advanceCursor(); })
    , m_tempo_map(tempo_map)
{
    setComponentID("cursor_overlay");
    setInterceptsMouseClicks(true, false);
}

// Stores discrete timeline mapping data pushed by EditorView::setState().
void CursorOverlay::setVisibleTimelineRange(common::core::TimeRange visible_timeline) noexcept
{
    m_visible_timeline = visible_timeline;
}

// Stores the grid note value pushed by EditorView::setState(), so click snapping always uses the
// same grid the timeline and ruler render.
void CursorOverlay::setGridNoteValue(common::core::Fraction grid_note_value) noexcept
{
    m_grid_note_value = grid_note_value;
}

// Draws the time-selection wash (behind everything), then the cursor, then the snap guide; static
// waveform content remains in the views below.
void CursorOverlay::paint(juce::Graphics& g)
{
    if (m_time_selection.has_value())
    {
        // The grid-locked span in seconds maps to pixels with the same helper as the cursor, so the
        // wash tracks zoom/scroll and clamps to the visible edges when an endpoint scrolls off.
        const std::optional<float> left =
            cursorXForTimelinePosition(m_time_selection->start, m_visible_timeline, getWidth());
        const std::optional<float> right =
            cursorXForTimelinePosition(m_time_selection->end, m_visible_timeline, getWidth());
        if (left.has_value() && right.has_value())
        {
            const float x0 = std::min(*left, *right);
            const float x1 = std::max(*left, *right);
            const auto height = static_cast<float>(getHeight());
            const juce::Colour accent = editorTheme().accent;
            g.setColour(accent.withAlpha(0.14f));
            g.fillRect(juce::Rectangle<float>{x0, 0.0f, x1 - x0, height});
            // Crisp 1px boundaries mark the two grid-locked edges (top/bottom are the canvas edges).
            g.setColour(accent.withAlpha(0.55f));
            g.fillRect(juce::Rectangle<float>{x0, 0.0f, 1.0f, height});
            g.fillRect(juce::Rectangle<float>{x1 - 1.0f, 0.0f, 1.0f, height});
        }
    }

    static_cast<void>(drawTimelineCursor(g, *this, m_cursor_x, 0, editorTheme().playback_cursor));

    if (m_snap_guide.has_value())
    {
        g.setColour(editorTheme().accent.withAlpha(0.85f));
        g.drawLine(m_snap_guide->x, 0.0f, m_snap_guide->x, static_cast<float>(getHeight()), 1.4f);
        if (m_snap_guide->label.isNotEmpty())
        {
            g.setFont(juce::FontOptions{12.0f});
            const juce::Rectangle<int> label_bounds{
                static_cast<int>(m_snap_guide->x) + 5, 2, 72, 16
            };
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRoundedRectangle(label_bounds.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.drawText(m_snap_guide->label, label_bounds, juce::Justification::centredLeft, true);
        }
    }
}

// Shows or clears the transient snap guide reported by a track-row drag.
void CursorOverlay::setSnapGuide(std::optional<TimelineSnapGuide> guide)
{
    if (m_snap_guide == guide)
    {
        return;
    }

    m_snap_guide = std::move(guide);
    repaint();
}

// Shows or clears the grid-locked time-selection wash. Stored in seconds and mapped at paint time,
// so a zoom or scroll re-maps it without a fresh push; only a range change repaints.
void CursorOverlay::setTimeSelectionRange(std::optional<common::core::TimeRange> range)
{
    if (m_time_selection == range)
    {
        return;
    }

    m_time_selection = range;
    repaint();
}

// Installs the predicate that lets track-row pointer targets receive clicks.
void CursorOverlay::setHitTestPassThrough(std::function<bool(juce::Point<int>)> pass_through)
{
    m_hit_test_pass_through = std::move(pass_through);
}

void CursorOverlay::setPausedCursorHidden(bool hidden) noexcept
{
    m_paused_cursor_hidden = hidden;
}

void CursorOverlay::setSeekBandHeight(int height) noexcept
{
    m_seek_band_height = height;
}

// Claims pointer events except where the pass-through predicate declines them.
bool CursorOverlay::hitTest(int x, int y)
{
    if (m_hit_test_pass_through && m_hit_test_pass_through(juce::Point<int>{x, y}))
    {
        return false;
    }

    return true;
}

// Converts highway-band timeline clicks into timeline seek intent. Clicks below the band —
// the tone strip and automation lanes — never seek or move the position (the marker model:
// those surfaces own their clicks).
void CursorOverlay::mouseDown(const juce::MouseEvent& event)
{
    if (getWidth() <= 0 || !event.mods.isLeftButtonDown() ||
        (m_seek_band_height > 0 && event.position.y > static_cast<float>(m_seek_band_height)))
    {
        return;
    }

    const std::optional<common::core::TimePosition> position = core::timelineCursorPlacementTime(
        m_tempo_map,
        m_grid_note_value,
        m_visible_timeline,
        getWidth(),
        event.position.x,
        placementModeFor(event.mods));
    if (position.has_value())
    {
        m_controller.onTimelineSeekRequested(*position);
    }
}

// Samples the current position at render cadence and invalidates only changed cursor strips.
// With a chart displayed the line renders only during playback (the marker model,
// 2026-07-18): while paused the position shows as the caret or the ruler's mark, keeping the
// lane clear; chartless arrangements keep the paused line as their only indicator.
void CursorOverlay::advanceCursor()
{
    const bool cursor_visible = m_transport.state().playing || !m_paused_cursor_hidden;
    const std::optional<float> next_cursor_x =
        cursor_visible
            ? cursorXForTimelinePosition(m_transport.position(), m_visible_timeline, getWidth())
            : std::nullopt;

    if (next_cursor_x == m_cursor_x)
    {
        return;
    }

    repaintCursorStrip(*this, m_cursor_x, next_cursor_x);
    m_cursor_x = next_cursor_x;
}

} // namespace rock_hero::editor::ui
