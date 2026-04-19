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

// Creates the Tracktion SmartThumbnail behind the public Tracktion-free wrapper.
Thumbnail::Thumbnail(tracktion::Engine& engine, juce::Component& owner)
    : m_impl(std::make_unique<Impl>(engine, owner))
{
}

// Uses the default destruction order so the Tracktion thumbnail releases before its engine.
Thumbnail::~Thumbnail() = default;

// Starts proxy generation for the selected file and caches duration for UI coordinate mapping.
void Thumbnail::setFile(const std::filesystem::path& file)
{
    const juce::File juce_file(file.string());
    tracktion::AudioFile audio_file(m_impl->engine, juce_file);
    m_impl->total_length_seconds = audio_file.getLength();
    m_impl->thumbnail.setNewFile(audio_file);
}

// Exposes Tracktion proxy-generation state so the UI can show progress instead of stale audio.
bool Thumbnail::isGeneratingProxy() const
{
    return m_impl->thumbnail.isGeneratingProxy();
}

// Exposes Tracktion proxy progress as a simple fraction for UI status text.
float Thumbnail::getProxyProgress() const
{
    return m_impl->thumbnail.getProxyProgress();
}

// Returns cached file duration so UI code does not need to ask Tracktion directly.
double Thumbnail::getLength() const
{
    return m_impl->total_length_seconds;
}

// Draws the full cached audio file into the requested bounds for the current editor view.
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
