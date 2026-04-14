/*!
\file waveform_display.h
\brief Waveform rendering component with a scrolling playhead cursor.
*/

#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

class AudioEngine;

/*!
\brief Displays the waveform of the loaded audio file with a scrolling playhead.

Uses a Pimpl (Impl struct) to hide tracktion::SmartThumbnail from this header, keeping Tracktion
headers out of consuming translation units.

A private 60 Hz juce::Timer drives repaint during playback and proxy generation. On each tick
the timer also calls AudioEngine::updateTransportPositionCache() to mirror the transport
position into the lock-free atomic cache.

\see AudioEngine
*/
class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    /*!
    \brief Creates the waveform display and starts the 60 Hz repaint timer.
    \param engine The AudioEngine whose transport state drives the cursor.
    */
    explicit WaveformDisplay(AudioEngine& engine);

    /*!
    \brief Stops the timer and destroys the Pimpl.
    */
    ~WaveformDisplay() override;

    /*!
    \brief Updates the thumbnail source after a new file has been loaded.
    \param file The audio file that was successfully loaded into the engine.
    */
    void setAudioFile(const juce::File& file);

    /// Called with the target position in seconds when the user clicks the waveform.
    /// Only fires when a file is loaded (m_total_length_seconds > 0).
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

    AudioEngine& m_audio_engine;

    // Normalised cursor position in the range [0, 1].
    double m_cursor_proportion{0.0};

    // Duration of the loaded audio file in seconds.
    double m_total_length_seconds{0.0};

    // Impl exists specifically to keep Tracktion-only types out of this public header.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero
