/*!
\file waveform_display.h
\brief Waveform rendering component with a scrolling playhead cursor.
*/

#pragma once

#include <filesystem>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/audio/engine.h>

namespace rock_hero::audio
{
class Thumbnail;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

/*!
\brief Displays the waveform of the loaded audio file with a scrolling playhead.

Delegates thumbnail proxy generation and drawing to audio::Thumbnail, which keeps all Tracktion
types out of this library. Transport cursor updates arrive through an audio::Engine
subscription; thumbnail loading repaints are requested by the thumbnail adapter.

\see rock_hero::audio::Engine
\see rock_hero::audio::Thumbnail
*/
class WaveformDisplay : public juce::Component
{
public:
    /*!
    \brief Creates the waveform display and subscribes to transport position changes.
    \param engine The audio engine whose transport state drives the cursor.
    */
    explicit WaveformDisplay(audio::Engine& engine);

    /*! \brief Stops the timer and releases resources. */
    ~WaveformDisplay() override;

    /*!
    \brief Updates the thumbnail source after a new file has been loaded.
    \param file The audio file that was successfully loaded into the engine.
    */
    void setAudioFile(const std::filesystem::path& file);

    /*!
    \brief Called with the target position in seconds when the user clicks the waveform.

    Only fires when a file is loaded and the thumbnail length is greater than zero.
    */
    std::function<void(double)> on_seek;

    /*!
    \brief Seeks to the clicked position and fires on_seek.
    \param event The mouse event from the click.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Draws the waveform thumbnail and the playhead cursor line.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Responds to component size changes. */
    void resized() override;

private:
    void setTransportPosition(double seconds);

    std::unique_ptr<audio::Thumbnail> m_thumbnail;
    audio::Engine::Subscription m_transport_position_subscription;

    // Normalised cursor position in the range [0, 1].
    double m_cursor_proportion{0.0};
};

} // namespace rock_hero::ui
