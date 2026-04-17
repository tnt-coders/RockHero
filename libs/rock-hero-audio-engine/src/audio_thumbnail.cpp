#include <rock_hero_audio_engine/audio_thumbnail.h>

#include <rock_hero_audio_engine/audio_engine.h>

#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero
{

struct AudioThumbnail::Impl
{
    AudioEngine& engine;
    tracktion::SmartThumbnail thumbnail;
    double total_length_seconds{0.0};

    Impl(AudioEngine& eng, juce::Component& owner)
        : engine(eng),
          thumbnail(eng.getEngine(), tracktion::AudioFile(eng.getEngine()), owner, nullptr)
    {
    }
};

AudioThumbnail::AudioThumbnail(AudioEngine& engine, juce::Component& owner)
    : m_impl(std::make_unique<Impl>(engine, owner))
{
}

AudioThumbnail::~AudioThumbnail() = default;

void AudioThumbnail::setFile(const std::filesystem::path& file)
{
    const juce::File juce_file(file.string());
    tracktion::AudioFile audio_file(m_impl->engine.getEngine(), juce_file);
    m_impl->total_length_seconds = audio_file.getLength();
    m_impl->thumbnail.setNewFile(audio_file);
}

bool AudioThumbnail::isGeneratingProxy() const
{
    return m_impl->thumbnail.isGeneratingProxy();
}

float AudioThumbnail::getProxyProgress() const
{
    return m_impl->thumbnail.getProxyProgress();
}

double AudioThumbnail::getLength() const
{
    return m_impl->total_length_seconds;
}

void AudioThumbnail::drawChannels(
    juce::Graphics& g, juce::Rectangle<int> bounds, float vertical_zoom) const
{
    const tracktion::TimeRange visible_range{
        tracktion::TimePosition{},
        tracktion::TimePosition::fromSeconds(m_impl->total_length_seconds)
    };
    m_impl->thumbnail.drawChannels(g, bounds, visible_range, vertical_zoom);
}

} // namespace rock_hero