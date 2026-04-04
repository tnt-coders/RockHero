#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

class AudioEngine;

// Displays the waveform of the loaded audio file with a scrolling playhead cursor.
// Owns a tracktion::SmartThumbnail via PIMPL to keep Tracktion headers out of this file.
// Runs a 60 Hz timer to drive repaints during playback.
class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    explicit WaveformDisplay(AudioEngine& engine);
    ~WaveformDisplay() override;

    // Call after a new file is successfully loaded to update the thumbnail source.
    void setAudioFile(const juce::File& file);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    AudioEngine& m_audio_engine;

    // PIMPL: hides tracktion::SmartThumbnail so Tracktion headers stay out of this .h.
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    double m_cursor_proportion{0.0};
    double m_total_length_seconds{0.0};
};

} // namespace rock_hero
