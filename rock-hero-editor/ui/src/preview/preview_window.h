/*!
\file preview_window.h
\brief Top-level editor window hosting the 3D highway preview (plan 44).
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/highway/highway_view_state.h>

namespace rock_hero::editor::ui
{

class PreviewSurface;

/*!
\brief Resizable secondary window showing the exact highway the game renders for the current
arrangement.

Owned by EditorView like the other secondary windows and toggled from the View menu (or F3).
Show resumes the render surface and hide suspends its frame ticks: a hidden JUCE top-level
keeps its native peer (and the embedded render child) alive, but the vblank feed is
visibility-blind, so the ticks stop explicitly while the window is away. The GPU stack itself
lives from first open until destruction because bgfx cannot re-initialize in-process.
Transport keys pressed while the preview has focus forward to the main view (44-Q4), so
play/pause works without refocusing the editor.
*/
class PreviewWindow final : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the hidden preview window.
    \param transport Read-only transport for paused-cursor time.
    \param playback_clock Playback-time telemetry sampled while playing.
    \param forward_key_press Callback that offers key presses to the main view's handler.
    \param centering_component Optional component used to position the window on first show.
    */
    PreviewWindow(
        const common::audio::ITransport& transport,
        const common::audio::IPlaybackClock& playback_clock,
        std::function<bool(const juce::KeyPress&)> forward_key_press,
        juce::Component* centering_component);

    /*! \brief Hides the window (detaching the render surface first). */
    ~PreviewWindow() override;

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;
    PreviewWindow(PreviewWindow&&) = delete;
    PreviewWindow& operator=(PreviewWindow&&) = delete;

    /*! \brief Shows the window and brings the render surface up (or resumes its ticks). */
    void open();

    /*! \brief Suspends the render surface's frame ticks and hides the window. */
    void close();

    /*!
    \brief Replaces the highway content shown by the surface.
    \param state Shared scene-model snapshot from the controller; null clears the board.
    */
    void setHighwayState(std::shared_ptr<const common::core::HighwayViewState> state);

    /*! \brief Title-bar close behaves like the View-menu toggle turning the preview off. */
    void closeButtonPressed() override;

    /*!
    \brief Offers unhandled keys to the main view (transport shortcuts keep working here).
    \param key Pressed key.
    \return True when the forwarded handler consumed the key.
    */
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // Owned by the DocumentWindow as its content component.
    PreviewSurface* m_surface{nullptr};

    std::function<bool(const juce::KeyPress&)> m_forward_key_press;
};

} // namespace rock_hero::editor::ui
