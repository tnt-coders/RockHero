#include <rock_hero_ui/waveform_display.h>

#include <algorithm>

#include <rock_hero_audio_engine/audio_engine.h>

// TODO: Move SmartThumbnail management into AudioEngine so WaveformDisplay only depends on JUCE
//       types. Then this include can be removed.
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace rock_hero
{

struct WaveformDisplay::Impl
{
    te::SmartThumbnail thumbnail;

    Impl(te::Engine& engine, juce::Component& owner)
        : thumbnail(engine, te::AudioFile(engine), owner, nullptr)
    {
    }
};

WaveformDisplay::WaveformDisplay(AudioEngine& engine)
    : m_audio_engine(engine), m_impl(std::make_unique<Impl>(engine.getEngine(), *this))
{
    startTimerHz(60);
}

WaveformDisplay::~WaveformDisplay() = default;

void WaveformDisplay::setAudioFile(const juce::File& file)
{
    te::AudioFile audio_file(m_audio_engine.getEngine(), file);
    m_total_length_seconds = audio_file.getLength();
    m_impl->thumbnail.setNewFile(audio_file);
    repaint();
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll(juce::Colours::black);

    if (m_total_length_seconds <= 0.0)
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Load a file to see the waveform", bounds, juce::Justification::centred);
        return;
    }

    if (m_impl->thumbnail.isGeneratingProxy())
    {
        const auto pct = static_cast<int>(m_impl->thumbnail.getProxyProgress() * 100.0f);
        g.setColour(juce::Colours::white);
        g.drawText(
            "Building waveform: " + juce::String(pct) + "%", bounds, juce::Justification::centred);
        return;
    }

    const te::TimeRange visible_range{
        te::TimePosition{}, te::TimePosition::fromSeconds(m_total_length_seconds)
    };
    g.setColour(juce::Colours::lightgreen);
    m_impl->thumbnail.drawChannels(g, bounds, visible_range, 1.0f);

    // Scrolling playhead cursor.
    const auto cursor_x =
        static_cast<float>(bounds.getWidth()) * static_cast<float>(m_cursor_proportion);
    g.setColour(juce::Colours::white);
    g.drawLine(cursor_x, 0.0f, cursor_x, static_cast<float>(bounds.getHeight()), 2.0f);
}

void WaveformDisplay::resized()
{
    // SmartThumbnail draws into the bounds passed to drawChannels — no child layout needed.
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    if (m_total_length_seconds <= 0.0 || !on_seek)
    {
        return;
    }

    const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
    const double clamped = std::clamp(ratio, 0.0, 1.0);

    // Update cursor immediately so the playhead moves before the 60 Hz tick fires.
    m_cursor_proportion = clamped;
    repaint();

    on_seek(clamped * m_total_length_seconds);
}

void WaveformDisplay::timerCallback()
{
    m_audio_engine.updateTransportPositionCache();

    const double pos = m_audio_engine.getTransportPosition();
    // Keep cursor math in normalised space so paint() only needs the current bounds to translate
    // transport time into screen coordinates.
    const double new_proportion =
        (m_total_length_seconds > 0.0) ? pos / m_total_length_seconds : 0.0;

    const bool cursor_moved = new_proportion != m_cursor_proportion;
    m_cursor_proportion = new_proportion;

    if (cursor_moved || m_audio_engine.isPlaying() || m_impl->thumbnail.isGeneratingProxy())
    {
        repaint();
    }
}

} // namespace rock_hero
