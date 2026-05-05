#include "tracktion_thumbnail.h"

#include <cmath>

namespace rock_hero::audio
{

namespace
{

// Rejects ranges that Tracktion cannot draw safely for the currently loaded source.
[[nodiscard]] bool isValidSourceRange(core::TimeRange source_range, double source_length) noexcept
{
    return std::isfinite(source_range.start.seconds) && std::isfinite(source_range.end.seconds) &&
           source_range.start.seconds >= 0.0 &&
           source_range.end.seconds > source_range.start.seconds &&
           source_range.end.seconds <= source_length;
}

} // namespace

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
    m_source_length_seconds = audio_file.getLength();
    m_has_source = m_source_length_seconds > 0.0;
    m_thumbnail.setNewFile(audio_file);
}

// Reports whether the most recent source assignment produced drawable source data.
bool TracktionThumbnail::hasSource() const
{
    return m_has_source;
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

// Draws the requested source range after validating it against the loaded source asset.
bool TracktionThumbnail::drawChannels(
    juce::Graphics& g, juce::Rectangle<int> bounds, core::TimeRange source_range,
    float vertical_zoom)
{
    if (!m_has_source || !isValidSourceRange(source_range, m_source_length_seconds))
    {
        return false;
    }

    const tracktion::TimeRange visible_range(
        tracktion::TimePosition::fromSeconds(source_range.start.seconds),
        tracktion::TimePosition::fromSeconds(source_range.end.seconds));
    m_thumbnail.drawChannels(g, bounds, visible_range, vertical_zoom);
    return true;
}

} // namespace rock_hero::audio
