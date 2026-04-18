#include "waveform_display.h"

#include <algorithm>
#include <rock_hero/audio/engine.h>
#include <rock_hero/audio/thumbnail.h>

namespace rock_hero::ui
{

WaveformDisplay::WaveformDisplay(audio::Engine& engine)
    : m_audio_engine(engine), m_thumbnail(engine.createThumbnail(*this))
{
    startTimerHz(60);
}

WaveformDisplay::~WaveformDisplay() = default;

void WaveformDisplay::setAudioFile(const std::filesystem::path& file)
{
    m_thumbnail->setFile(file);
    repaint();
}

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

void WaveformDisplay::resized()
{
    // WaveformThumbnail draws into the bounds passed to drawChannels — no child layout needed.
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    const double length = m_thumbnail->getLength();
    if (length <= 0.0 || !on_seek)
    {
        return;
    }

    const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
    const double clamped = std::clamp(ratio, 0.0, 1.0);

    // Update cursor immediately so the playhead moves before the 60 Hz tick fires.
    m_cursor_proportion = clamped;
    repaint();

    on_seek(clamped * length);
}

void WaveformDisplay::timerCallback()
{
    m_audio_engine.updateTransportPositionCache();

    const double pos = m_audio_engine.getTransportPosition();
    const double length = m_thumbnail->getLength();
    // Keep cursor math in normalised space so paint() only needs the current bounds to translate
    // transport time into screen coordinates.
    const double new_proportion = (length > 0.0) ? pos / length : 0.0;

    const bool cursor_moved = new_proportion != m_cursor_proportion;
    m_cursor_proportion = new_proportion;

    if (cursor_moved || m_audio_engine.isPlaying() || m_thumbnail->isGeneratingProxy())
    {
        repaint();
    }
}

} // namespace rock_hero::ui
