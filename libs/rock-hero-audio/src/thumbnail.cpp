#include "thumbnail.h"

#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::audio
{

struct Thumbnail::Impl
{
    tracktion::Engine& engine;
    tracktion::SmartThumbnail thumbnail;
    double total_length_seconds{0.0};

    Impl(tracktion::Engine& eng, juce::Component& owner)
        : engine(eng), thumbnail(eng, tracktion::AudioFile(eng), owner, nullptr)
    {
    }
};

Thumbnail::Thumbnail(tracktion::Engine& engine, juce::Component& owner)
    : m_impl(std::make_unique<Impl>(engine, owner))
{
}

Thumbnail::~Thumbnail() = default;

void Thumbnail::setFile(const std::filesystem::path& file)
{
    const juce::File juce_file(file.string());
    tracktion::AudioFile audio_file(m_impl->engine, juce_file);
    m_impl->total_length_seconds = audio_file.getLength();
    m_impl->thumbnail.setNewFile(audio_file);
}

bool Thumbnail::isGeneratingProxy() const
{
    return m_impl->thumbnail.isGeneratingProxy();
}

float Thumbnail::getProxyProgress() const
{
    return m_impl->thumbnail.getProxyProgress();
}

double Thumbnail::getLength() const
{
    return m_impl->total_length_seconds;
}

void Thumbnail::drawChannels(
    juce::Graphics& g, juce::Rectangle<int> bounds, float vertical_zoom) const
{
    const tracktion::TimeRange visible_range{
        tracktion::TimePosition{},
        tracktion::TimePosition::fromSeconds(m_impl->total_length_seconds)
    };
    m_impl->thumbnail.drawChannels(g, bounds, visible_range, vertical_zoom);
}

} // namespace rock_hero::audio
