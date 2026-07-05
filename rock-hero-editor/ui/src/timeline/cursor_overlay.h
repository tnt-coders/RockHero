/*!
\file cursor_overlay.h
\brief Editor-wide timeline cursor and seek overlay drawn above the zoomable track canvas.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/i_editor_controller.h>

namespace rock_hero::editor::ui
{

/*!
\brief Handles editor-wide timeline interaction and draws the cursor from transport position.

The overlay samples current position through a read-only transport at vblank cadence, so cursor
motion never routes through controller state pushes. Static waveform content stays in the track
views below the overlay.
*/
class CursorOverlay final : public juce::Component
{
public:
    /*!
    \brief Starts vblank-driven cursor refresh against the injected read-only transport.

    \param controller Controller that receives timeline seek intents.
    \param transport Read-only transport sampled at vblank cadence for its current position.
    \param tempo_map Tempo map used to snap timeline clicks; referenced, not copied, so the owner
    must keep it alive for this overlay's lifetime.
    */
    CursorOverlay(
        core::IEditorController& controller, const common::audio::ITransport& transport,
        const common::core::TempoMap& tempo_map);

    /*!
    \brief Stores the discrete timeline mapping range pushed by editor state.
    \param visible_timeline Visible timeline range used to map cursor position to pixels.
    */
    void setVisibleTimelineRange(common::core::TimeRange visible_timeline) noexcept;

    /*!
    \brief Stores the grid note value so click snapping matches the rendered timeline grid.
    \param grid_note_value Grid step as a fraction of a whole note.
    */
    void setGridNoteValue(common::core::Fraction grid_note_value) noexcept;

    /*!
    \brief Draws only the cursor; static waveform content remains in the views below.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*!
    \brief Converts editor-wide timeline clicks into timeline seek intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseDown(const juce::MouseEvent& event) override;

private:
    // Samples the current position at render cadence and invalidates only changed cursor strips.
    void advanceCursor();

    // Controller receives editor-level timeline seek intent.
    core::IEditorController& m_controller;

    // Read-only transport source sampled at vblank cadence for its current position method.
    const common::audio::ITransport& m_transport;

    // Vblank-driven callback used to keep cursor motion smooth without transport listeners.
    juce::VBlankAttachment m_vblank_attachment;

    // Visible timeline range last pushed by the owning view's setState().
    common::core::TimeRange m_visible_timeline{};

    // Tempo map owned by the editor view state, referenced to snap non-modified timeline clicks.
    const common::core::TempoMap& m_tempo_map;

    // Grid step as a fraction of a whole note, initialized to the quarter-note default because the
    // Fraction default of 0/1 is a degenerate step.
    common::core::Fraction m_grid_note_value{1, 4};

    // Last subpixel cursor x coordinate drawn by the overlay, if a cursor is currently mappable.
    std::optional<float> m_cursor_x{};
};

} // namespace rock_hero::editor::ui
