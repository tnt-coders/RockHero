/*!
\file preview_window.h
\brief Top-level editor window hosting the 3D highway preview.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/highway_theme/loaded_highway_theme.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::editor::ui
{

class PreviewSurface;

// Forward-declared so this header stays clear of the main window's private filter header; the
// constructor's definition includes it.
class ComposedCharacterFilter;

/*!
\brief Resizable secondary window showing the exact highway the game renders for the current
arrangement.

Owned by EditorView like the other secondary windows and toggled from the View menu (or F3).
Show resumes the render surface and hide suspends its frame ticks: a hidden JUCE top-level
keeps its native peer (and the embedded render child) alive, but the vblank feed is
visibility-blind, so the ticks stop explicitly while the window is away. The GPU stack itself
lives from first open until destruction because bgfx cannot re-initialize in-process.
Transport and song-navigation keys pressed while the preview has focus forward to the main view,
so play/pause and caret navigation work without refocusing the editor.
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
    \param theme The theme the highway draws with until \ref setLook; must outlive the window.
    \param palette The string colors the highway draws with until \ref setLook.
    */
    PreviewWindow(
        const common::audio::ITransport& transport,
        const common::audio::IPlaybackClock& playback_clock,
        std::function<bool(const juce::KeyPress&)> forward_key_press,
        juce::Component* centering_component, const common::ui::LoadedHighwayTheme& theme,
        const common::ui::StringColorPalette& palette);

    /*!
    \brief Detaches the key listener, then runs close(), which suspends the surface and hides the
    window.

    Detaching first: the key-listener list holds a non-owning pointer to the filter this window
    owns.
    */
    ~PreviewWindow() override;

    /*! \brief Copying is disabled because JUCE window ownership is not copyable. */
    PreviewWindow(const PreviewWindow&) = delete;

    /*! \brief Copy assignment is disabled because JUCE window ownership is not copyable. */
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    /*! \brief Moving is disabled because the listener registration is identity-based. */
    PreviewWindow(PreviewWindow&&) = delete;

    /*! \brief Move assignment is disabled because the listener registration is identity-based. */
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

    /*!
    \brief Forwards a refusal's highway marks to the surface, which flashes them.
    \param highlight The refused marks in the current snapshot's indices, and their color; null or
           empty when the refusal lights nothing on the board.
    */
    void flashRefusal(std::shared_ptr<const common::ui::HighwayHighlight> highlight);

    /*! \brief Re-frames the board from rest, for a different project or arrangement. */
    void resetCamera();

    /*!
    \brief Sets how the board maps onto the screen: the player's reading choices and the lane
    minimum the 2D tab pads to.
    \param options Display mapping; an unchanged one costs nothing.
    */
    void setDisplayOptions(common::core::HighwayDisplayOptions options);

    /*!
    \brief Switches the highway to another look.
    \param theme The theme to draw with; must outlive the window.
    \param palette The string colors to draw with.
    */
    void setLook(
        const common::ui::LoadedHighwayTheme& theme, const common::ui::StringColorPalette& palette);

    /*!
    \brief Publishes the timeline's paused mark (TrackViewport::pausedMarkSeconds) to the surface.
    \param seconds The paused mark's seconds, or nullopt while it is the transport's rest.
    */
    void setPausedMarkSeconds(std::optional<double> seconds);

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

    // Key listener that drops OS-composed characters before this window forwards a press to the
    // editor's mapping set. Held by pointer because the key-listener list is non-owning, and
    // heap-allocated because the type stays private to the UI library.
    std::unique_ptr<ComposedCharacterFilter> m_composed_character_filter;
};

} // namespace rock_hero::editor::ui
