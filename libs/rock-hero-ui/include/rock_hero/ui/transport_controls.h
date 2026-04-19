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
The owner wires on_play, on_pause, and on_stop, then calls setFileLoaded() and
setPlaying() to keep button state in sync.

Icons are SVGs embedded via juce_add_binary_data (BinaryData::play_arrow_svg, pause_svg,
stop_svg). Buttons use DrawableButton with ImageFitted style.
*/
class TransportControls : public juce::Component
{
public:
    TransportControls();
    ~TransportControls() override;

    /*! \brief Called when the user clicks Play while a file is loaded and playback is stopped. */
    std::function<void()> on_play;

    /*! \brief Called when the user clicks Pause during playback. */
    std::function<void()> on_pause;

    /*! \brief Called when the user clicks Stop while a file is loaded. */
    std::function<void()> on_stop;

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
    \brief Syncs the play/pause button icon to the current playback state.
    \param playing True if the engine is currently playing.
    */
    void setPlaying(bool playing);

    /*!
    \brief Handles a play/pause toggle, firing on_play or on_pause as appropriate.

    Called directly by the Space key handler so the hotkey and the button share one path.
    */
    void onPlayPauseClicked();

    /*! \brief Lays out the transport buttons within the component bounds. */
    void resized() override;

private:
    bool m_is_playing{false};
    bool m_file_loaded{false};

    std::unique_ptr<juce::DrawableButton> m_play_pause_button;
    std::unique_ptr<juce::DrawableButton> m_stop_button;

    std::unique_ptr<juce::Drawable> m_play_drawable;
    std::unique_ptr<juce::Drawable> m_pause_drawable;
    std::unique_ptr<juce::Drawable> m_stop_drawable;
};

} // namespace rock_hero::ui
