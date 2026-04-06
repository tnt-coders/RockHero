/** @file waveform_display.h
    @brief Waveform rendering component with a scrolling playhead cursor.
*/

#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

class AudioEngine;

/** Displays the waveform of the loaded audio file with a scrolling playhead.

    Uses a Pimpl (Impl struct) to hide tracktion::SmartThumbnail from this header, keeping Tracktion
    headers out of consuming translation units.

    A private 60 Hz juce::Timer drives repaint during playback and proxy generation. On each tick
    the timer also calls AudioEngine::updateTransportPositionCache() to mirror the transport
    position into the lock-free atomic cache.

    @see AudioEngine
*/
class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    /** Creates the waveform display and starts the 60 Hz repaint timer.
        @param engine  the AudioEngine whose transport state drives the cursor
    */
    explicit WaveformDisplay(AudioEngine& engine);

    /** Stops the timer and destroys the Pimpl. */
    ~WaveformDisplay() override;

    /** Updates the thumbnail source after a new file has been loaded.
        @param file  the audio file that was successfully loaded into the engine
    */
    void setAudioFile(const juce::File& file);

    /** Draws the waveform thumbnail and the playhead cursor line. */
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    /** Called at 60 Hz. Updates the transport position cache and repaints
        when playing or while the thumbnail proxy is being generated.
    */
    void timerCallback() override;

    AudioEngine& m_audio_engine;

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    double m_cursor_proportion{0.0};    ///< Normalised cursor position [0, 1].
    double m_total_length_seconds{0.0}; ///< Duration of the loaded audio file.
};

} // namespace rock_hero
