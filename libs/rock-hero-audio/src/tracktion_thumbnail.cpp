#include "tracktion_thumbnail.h"

namespace rock_hero::audio
{

// Creates the Tracktion SmartThumbnail behind the Tracktion-free IThumbnail interface.
TracktionThumbnail::TracktionThumbnail(tracktion::Engine& engine, juce::Component& owner)
    : m_engine(engine)
    , m_thumbnail(engine, tracktion::AudioFile(engine), owner, nullptr)
{}

// Translates the project-owned asset path into JUCE/Tracktion file types at the adapter boundary.
void TracktionThumbnail::setSource(const core::AudioAsset& audio_asset)
{
    const auto path_text = audio_asset.path.wstring();
    const juce::File file{juce::String{path_text.c_str()}};
    const tracktion::AudioFile audio_file(m_engine, file);
    m_total_length_seconds = audio_file.getLength();
    m_thumbnail.setNewFile(audio_file);
}

// Exposes Tracktion proxy-generation state so the UI can show progress instead of stale audio.
bool TracktionThumbnail::isGeneratingProxy() const
{
    return m_thumbnail.isGeneratingProxy();
}

// Exposes Tracktion proxy progress as a simple fraction for UI status text.
float TracktionThumbnail::getProxyProgress() const
{
    return m_thumbnail.getProxyProgress();
}

// Returns cached file duration so UI code does not need to ask Tracktion directly.
double TracktionThumbnail::getLength() const
{
    return m_total_length_seconds;
}

// Draws the full cached audio file into the requested bounds for the current editor view.
void TracktionThumbnail::drawChannels(
    juce::Graphics& g, juce::Rectangle<int> bounds, float vertical_zoom)
{
    const tracktion::TimeRange visible_range(
        tracktion::TimePosition::fromSeconds(0.0),
        tracktion::TimePosition::fromSeconds(m_total_length_seconds));
    m_thumbnail.drawChannels(g, bounds, visible_range, vertical_zoom);
}

} // namespace rock_hero::audio
