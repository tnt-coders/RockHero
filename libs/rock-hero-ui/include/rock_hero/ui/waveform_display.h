/*!
\file waveform_display.h
\brief Waveform rendering component with a scrolling playhead cursor.
*/

#pragma once

#include <filesystem>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::audio
{
class Engine;
class Thumbnail;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

/*!
\brief Displays the waveform of the loaded audio file with a scrolling playhead.

Delegates thumbnail proxy generation and drawing to audio::Thumbnail, which keeps all Tracktion
types out of this library.

A private 60 Hz juce::Timer drives repaint during playback and proxy generation. On each tick
the timer also calls audio::Engine::updateTransportPositionCache() to mirror the transport
position into the lock-free atomic cache.

\see rock_hero::audio::Engine
\see rock_hero::audio::Thumbnail
*/
class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    /*!
    \brief Creates the waveform display and starts the 60 Hz repaint timer.
    \param engine The audio engine whose transport state drives the cursor.
    */
    explicit WaveformDisplay(audio::Engine& engine);

    /*!
    \brief Stops the timer and releases resources.
    */
    ~WaveformDisplay() override;

    /*!
    \brief Updates the thumbnail source after a new file has been loaded.
    \param file The audio file that was successfully loaded into the engine.
    */
    void setAudioFile(const std::filesystem::path& file);

    /// Called with the target position in seconds when the user clicks the waveform.
    /// Only fires when a file is loaded (thumbnail length > 0).
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

    /*!
    \brief Responds to component size changes.
    */
    void resized() override;

private:
    // Called at 60 Hz to update the transport position cache and trigger repaint work.
    // Repaints while playback is running or while the thumbnail proxy is still being generated.
    void timerCallback() override;

    audio::Engine& m_audio_engine;
    std::unique_ptr<audio::Thumbnail> m_thumbnail;

    // Normalised cursor position in the range [0, 1].
    double m_cursor_proportion{0.0};
};

} // namespace rock_hero::ui
