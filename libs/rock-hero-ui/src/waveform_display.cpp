#include "waveform_display.h"

#include <algorithm>
#include <rock_hero/audio/engine.h>
#include <rock_hero/audio/thumbnail.h>

namespace rock_hero::ui
{

// Creates the Tracktion-backed thumbnail wrapper and attaches a listener for transport position.
WaveformDisplay::WaveformDisplay(audio::Engine& engine)
    : m_thumbnail(engine.createThumbnail(*this)), m_engine_listener(engine, *this)
{
}

// Relies on member destruction to release thumbnail resources before the engine reference dies.
WaveformDisplay::~WaveformDisplay() = default;

// Points the thumbnail at the engine-accepted file and repaints immediately for feedback.
// The engine publishes position 0 from loadFile(), which drives the cursor reset via the
// listener; this method only needs to update the thumbnail source and request a repaint.
void WaveformDisplay::setAudioFile(const juce::File& file)
{
    m_thumbnail->setFile(file);
    repaint();
}

// Draws loading, proxy-generation, waveform, and cursor states in one lightweight component.
void WaveformDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll(juce::Colours::black);

    if (m_thumbnail->getLength() <= 0.0)
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Load a file to see the waveform", bounds, juce::Justification::centred);
        return;
    }

    if (m_thumbnail->isGeneratingProxy())
    {
        const auto pct = static_cast<int>(m_thumbnail->getProxyProgress() * 100.0f);
        g.setColour(juce::Colours::white);
        g.drawText(
            "Building waveform: " + juce::String(pct) + "%", bounds, juce::Justification::centred);
        return;
    }

    g.setColour(juce::Colours::lightgreen);
    m_thumbnail->drawChannels(g, bounds, 1.0f);

    // Scrolling playhead cursor.
    const auto cursor_x =
        static_cast<float>(bounds.getWidth()) * static_cast<float>(m_cursor_proportion);
    g.setColour(juce::Colours::white);
    g.drawLine(cursor_x, 0.0f, cursor_x, static_cast<float>(bounds.getHeight()), 2.0f);
}

// Documents the intentional absence of child layout while preserving the JUCE override point.
void WaveformDisplay::resized()
{
    // WaveformThumbnail draws into the bounds passed to drawChannels; no child layout needed.
}

// Converts waveform clicks into seek requests. The engine responds by publishing a new
// transport position, which drives the cursor update via engineTransportPositionChanged.
void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    const double length = m_thumbnail->getLength();
    if (length <= 0.0 || !on_seek)
    {
        return;
    }

    const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
    const double clamped = std::clamp(ratio, 0.0, 1.0);

    on_seek(clamped * length);
}

// Mirrors engine transport events into repaintable cursor state.
void WaveformDisplay::engineTransportPositionChanged(double seconds)
{
    const double length = m_thumbnail->getLength();
    // Keep cursor math in normalised space so paint() only needs the current bounds to translate
    // transport time into screen coordinates.
    const double new_proportion = (length > 0.0) ? seconds / length : 0.0;

    if (new_proportion != m_cursor_proportion)
    {
        m_cursor_proportion = new_proportion;
        repaint();
    }
}

} // namespace rock_hero::ui
