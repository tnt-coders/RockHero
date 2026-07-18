/*!
\file cursor_overlay.h
\brief Editor-wide timeline cursor and seek overlay drawn above the zoomable track canvas.
*/

#pragma once

#include <compare>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/controller/i_editor_controller.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*! \brief Transient full-height alignment guide shown while a track-row drag snaps. */
struct TimelineSnapGuide
{
    /*! \brief Guide x position in timeline-canvas coordinates. */
    float x{};

    /*! \brief Musical readout (for example "17:3") drawn beside the guide. */
    juce::String label;

    /*!
    \brief Compares two snap guides by their stored values.
    \param lhs Left-hand snap guide.
    \param rhs Right-hand snap guide.
    \return True when both guides store equal values.
    */
    friend bool operator==(const TimelineSnapGuide& lhs, const TimelineSnapGuide& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.x <=> rhs.x) && lhs.label == rhs.label;
    }
};

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
    \brief Shows or clears the transient snap guide reported by a track-row drag.
    \param guide Guide to draw, or empty to clear it.
    */
    void setSnapGuide(std::optional<TimelineSnapGuide> guide);

    /*!
    \brief Hides the paused cursor while a chart is displayed.

    With a chart the content-spanning line renders only during playback (the marker model,
    2026-07-18, revised the same day from passive-shows-the-line on user feedback: the paused
    line sat over selected notes' fret numbers): while paused the position shows as the armed
    caret or the ruler's aligned mark, never as lane furniture. Chartless arrangements keep
    their paused line as the only position indicator.

    \param hidden True while a chart is displayed.
    */
    void setPausedCursorHidden(bool hidden) noexcept;

    /*!
    \brief Restricts seek clicks to the highway band (the waveform/tab rows).

    Clicks below the band — the tone strip and automation lanes — are not seeks (the marker
    model: those surfaces own their clicks and never move the position).

    \param height Highway band height in overlay-local pixels; zero or less disables the gate.
    */
    void setSeekBandHeight(int height) noexcept;

    /*!
    \brief Installs the predicate that lets track-row pointer targets receive clicks.

    The overlay spans the whole canvas above every track row, so by default it consumes
    all clicks as seeks. Positions the predicate claims fall through to the row beneath.

    \param pass_through Predicate over overlay-local positions; empty restores full capture.
    */
    void setHitTestPassThrough(std::function<bool(juce::Point<int>)> pass_through);

    /*!
    \brief Claims pointer events except where the pass-through predicate declines them.
    \param x Pointer x position in local coordinates.
    \param y Pointer y position in local coordinates.
    \return True when the overlay should receive the pointer event.
    */
    bool hitTest(int x, int y) override;

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

    // Transient snap guide reported by an active track-row drag, if one is showing.
    std::optional<TimelineSnapGuide> m_snap_guide{};

    // Lets track-row pointer targets beneath the overlay receive clicks.
    std::function<bool(juce::Point<int>)> m_hit_test_pass_through;

    // True while a chart is displayed, hiding the paused line (the caret or the ruler mark is
    // the paused position display then).
    bool m_paused_cursor_hidden{false};

    // Highway band height gating seek clicks; zero or less accepts clicks anywhere.
    int m_seek_band_height{0};
};

} // namespace rock_hero::editor::ui
