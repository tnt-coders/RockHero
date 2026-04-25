/*!
\file transport_controls.h
\brief Play/pause/stop transport button strip.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::ui
{

/*!
\brief A horizontal strip containing Play/Pause and Stop transport buttons.

Interaction is entirely callback-based; this component has no knowledge of the audio engine.
The owner supplies play, pause, and stop callbacks through setter methods, then calls
setFileLoaded(), setPlaying(), and setTransportPosition() to keep button state in sync.

Icons are SVGs embedded via juce_add_binary_data (BinaryData::play_arrow_svg, pause_svg,
stop_svg). Buttons use DrawableButton with ImageFitted style.
*/
class TransportControls : public juce::Component
{
public:
    /*! \brief Creates icon-backed transport buttons in their disabled initial state. */
    TransportControls();

    /*! \brief Releases button and icon resources. */
    ~TransportControls() override;

    /*! \brief Copying is disabled because JUCE components and callback state are not copyable. */
    TransportControls(const TransportControls&) = delete;

    /*! \brief Copy assignment is disabled because JUCE components and callback state are not
     * copyable. */
    TransportControls& operator=(const TransportControls&) = delete;

    /*! \brief Moving is disabled because JUCE components and callback state are not movable. */
    TransportControls(TransportControls&&) = delete;

    /*! \brief Move assignment is disabled because JUCE components and callback state are not
     * movable. */
    TransportControls& operator=(TransportControls&&) = delete;

    /*!
    \brief Stores the callback fired when the user clicks Play from a stopped state.
    \param on_play Callback invoked for Play intent.
    */
    void setOnPlay(std::function<void()> on_play);

    /*!
    \brief Stores the callback fired when the user clicks Pause during playback.
    \param on_pause Callback invoked for Pause intent.
    */
    void setOnPause(std::function<void()> on_pause);

    /*!
    \brief Stores the callback fired when the user clicks Stop while a file is loaded.
    \param on_stop Callback invoked for Stop intent.
    */
    void setOnStop(std::function<void()> on_stop);

    /*!
    \brief Enables or disables the transport buttons.
    \param loaded True if an audio file is currently loaded.
    */
    void setFileLoaded(bool loaded);

    /*!
    \brief Reports whether the transport buttons are currently enabled.
    \return True if a file is loaded.
    */
    [[nodiscard]] bool isFileLoaded() const;

    /*!
    \brief Updates the cached transport position used to gate the Stop button.

    Stop is only useful when something would change: playback is active, or the cursor has
    moved away from the start. The component combines this position with the playing state
    to decide whether the Stop button should be enabled.

    \param seconds Current transport position in seconds.
    */
    void setTransportPosition(double seconds);

    /*!
    \brief Syncs the play/pause button icon to the current playback state.
    \param playing True if the engine is currently playing.
    */
    void setPlaying(bool playing);

    /*!
    \brief Handles a play/pause toggle, firing the stored play or pause callback as appropriate.

    Called directly by the Space key handler so the hotkey and the button share one path.
    */
    void onPlayPauseClicked();

    /*! \brief Lays out the transport buttons within the component bounds. */
    void resized() override;

private:
    // Applies current transport state to enabled flags without emitting callbacks.
    void updateButtonStates();

    // Callback fired when Play intent is emitted from the primary button.
    std::function<void()> m_on_play{};

    // Callback fired when Pause intent is emitted from the primary button.
    std::function<void()> m_on_pause{};

    // Callback fired when Stop intent is emitted from the secondary button.
    std::function<void()> m_on_stop{};

    // Cached playback state that decides whether the primary button shows Play or Pause.
    bool m_is_playing{false};

    // Tracks whether a loaded file exists, gating all user transport actions.
    bool m_file_loaded{false};

    // Cached cursor position used to decide whether Stop can change state.
    double m_transport_position{0.0};

    // Button that toggles between Play and Pause icons based on m_is_playing.
    std::unique_ptr<juce::DrawableButton> m_play_pause_button;

    // Button that sends Stop intent when playback or cursor state can be reset.
    std::unique_ptr<juce::DrawableButton> m_stop_button;

    // Embedded Play icon retained because DrawableButton stores non-owning image pointers.
    std::unique_ptr<juce::Drawable> m_play_drawable;

    // Embedded Pause icon retained because DrawableButton stores non-owning image pointers.
    std::unique_ptr<juce::Drawable> m_pause_drawable;

    // Embedded Stop icon retained because DrawableButton stores non-owning image pointers.
    std::unique_ptr<juce::Drawable> m_stop_drawable;
};

} // namespace rock_hero::ui
