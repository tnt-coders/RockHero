/*!
\file editor_view.h
\brief Concrete JUCE editor view that renders editor state and emits editor intents.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/core/timeline.h>
#include <rock_hero/ui/editor_view_state.h>
#include <rock_hero/ui/i_editor_controller.h>
#include <rock_hero/ui/i_editor_view.h>
#include <rock_hero/ui/track_view.h>
#include <rock_hero/ui/transport_controls.h>

namespace rock_hero::ui
{

/*!
\brief Computes a cursor x coordinate for a timeline position and visible range.

\param position Current transport position.
\param visible_timeline Visible timeline range.
\param width Drawing width in pixels.
\return Subpixel x coordinate in [0, width - 1], or empty when no cursor can be mapped.
*/
[[nodiscard]] std::optional<float> cursorXForTimelinePosition(
    core::TimePosition position, core::TimeRange visible_timeline, int width) noexcept;

/*!
\brief JUCE implementation of the editor view contract.

EditorView renders transition-shaped EditorViewState, owns concrete child widgets, and forwards
user intent to IEditorController. It also owns one editor-wide cursor overlay that reads current
position through a const audio::ITransport reference at vblank cadence; current cursor position is
not part of EditorViewState.
*/
class EditorView final : public juce::Component,
                         public IEditorView,
                         private TransportControls::Listener
{
public:
    /*!
    \brief Creates the concrete editor view and installs the thumbnail factory.
    \param controller Controller that receives all user intents emitted by this view.
    \param transport Read-only transport used by the cursor overlay for live position reads.
    \param thumbnail_factory Factory used by track-owned clip views to create thumbnails.
    */
    EditorView(
        IEditorController& controller, const audio::ITransport& transport,
        audio::IThumbnailFactory& thumbnail_factory);

    /*! \brief Releases child widgets, cursor overlay, and file chooser state. */
    ~EditorView() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    EditorView(const EditorView&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    EditorView& operator=(const EditorView&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    EditorView(EditorView&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    EditorView& operator=(EditorView&&) = delete;

    /*!
    \brief Applies transition-shaped editor state to child widgets.
    \param state State derived by the controller.
    */
    void setState(const EditorViewState& state) override;

    /*!
    \brief Paints the editor background behind child widgets.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out load controls, transport controls, waveform row, and timeline overlay. */
    void resized() override;

    /*!
    \brief Handles editor-level keyboard shortcuts.
    \param key Key press delivered by JUCE.
    \return True when the shortcut was handled.
    */
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class CursorOverlay;

    // Opens the asynchronous file chooser and forwards accepted selections to the controller.
    void onLoadClicked();

    // Presents a new load error once per error value.
    void presentLoadErrorIfNeeded(const std::optional<std::string>& error);

    // TransportControls::Listener implementation.
    void onPlayPausePressed() override;

    // TransportControls::Listener implementation.
    void onStopPressed() override;

    // Controller that owns editor workflow policy.
    IEditorController& m_controller;

    // Last state pushed by the controller; used for load target lookup and layout mapping.
    EditorViewState m_state{};

    // Button that launches the audio-file chooser for the first track.
    juce::TextButton m_load_button;

    // Concrete presentation-only transport control strip.
    TransportControls m_transport_controls;

    // Initial single waveform row used until later multi-row composition lands.
    TrackView m_track_view;

    // Editor-wide cursor and seek overlay drawn above the waveform row.
    std::unique_ptr<CursorOverlay> m_cursor_overlay;

    // Owned asynchronous file chooser; must outlive the native dialog callback.
    std::unique_ptr<juce::FileChooser> m_file_chooser;

    // Last error already shown to the user so repeated state pushes do not re-open dialogs.
    std::optional<std::string> m_last_presented_error{};
};

} // namespace rock_hero::ui
