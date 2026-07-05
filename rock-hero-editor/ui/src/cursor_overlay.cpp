#include "cursor_overlay.h"

#include "timeline_cursor.h"

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

// Draws only the cursor; static waveform content remains in ArrangementView below it.
void CursorOverlay::paint(juce::Graphics& g)
{
    drawTimelineCursor(g, *this, m_cursor_x, 0);
}

// Converts editor-wide timeline clicks into timeline seek intent.
void CursorOverlay::mouseDown(const juce::MouseEvent& event)
{
    if (getWidth() <= 0 || !event.mods.isLeftButtonDown())
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
void CursorOverlay::advanceCursor()
{
    const auto next_cursor_x =
        cursorXForTimelinePosition(m_transport.position(), m_visible_timeline, getWidth());

    if (next_cursor_x == m_cursor_x)
    {
        return;
    }

    repaintCursorStrip(*this, m_cursor_x, next_cursor_x);
    m_cursor_x = next_cursor_x;
}

} // namespace rock_hero::editor::ui
