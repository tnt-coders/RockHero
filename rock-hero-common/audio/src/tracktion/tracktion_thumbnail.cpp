#include "tracktion/tracktion_thumbnail.h"

#include <cmath>
#include <rock_hero/common/core/juce_path.h>

namespace rock_hero::common::audio
{

namespace
{

// Rejects ranges that Tracktion cannot draw safely for the currently loaded source.
[[nodiscard]] bool isValidVisibleRange(
    common::core::TimeRange visible_range, double source_length) noexcept
{
    return std::isfinite(visible_range.start.seconds) && std::isfinite(visible_range.end.seconds) &&
           visible_range.start.seconds >= 0.0 &&
           visible_range.end.seconds > visible_range.start.seconds &&
           visible_range.end.seconds <= source_length;
}

} // namespace

// Creates the Tracktion SmartThumbnail behind the Tracktion-free IThumbnail interface.
TracktionThumbnail::TracktionThumbnail(tracktion::Engine& engine, juce::Component& owner)
    : m_engine(engine)
    , m_thumbnail(engine, tracktion::AudioFile(engine), owner, nullptr)
{}

// Translates the project-owned asset path into JUCE/Tracktion file types at the adapter boundary.
// Tracktion identifies audio files for thumbnail caching by full-path hash only (see
// getAudioFileHash in tracktion_AudioFile.cpp), so a setNewFile call with the same path skips
// the change notification even when the file's bytes on disk have been replaced. Detect that
// case here and explicitly evict Tracktion's cached thumbnail entries so the waveform redraws
// against the new content immediately rather than waiting for whatever side path eventually
// invalidates the stale thumb.
void TracktionThumbnail::setSource(const common::core::AudioAsset& audio_asset)
{
    const juce::File file = common::core::juceFileFromPath(audio_asset.path);
    const tracktion::AudioFile audio_file(m_engine, file);
    m_source_length_seconds = audio_file.getLength();
    m_has_source = m_source_length_seconds > 0.0;

    const bool same_path_as_before = m_current_source_file == file;
    m_current_source_file = file;
    m_thumbnail.setNewFile(audio_file);
    if (same_path_as_before)
    {
        // forceFileUpdate() re-parses the file info, releases cached readers, and notifies all
        // active SmartThumbnails for this path. It is the public, message-thread-only API for
        // "the bytes at this path have changed". Does nothing if Tracktion has never loaded
        // the file before, which is fine because there is no stale cache to evict in that case.
        m_engine.getAudioFileManager().forceFileUpdate(audio_file);
    }
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

// Draws the requested visible range after validating it against the loaded source asset.
bool TracktionThumbnail::drawChannels(
    juce::Graphics& g, juce::Rectangle<int> bounds, common::core::TimeRange visible_range,
    float vertical_zoom)
{
    if (!m_has_source || !isValidVisibleRange(visible_range, m_source_length_seconds))
    {
        return false;
    }

    const tracktion::TimeRange tracktion_visible_range(
        tracktion::TimePosition::fromSeconds(visible_range.start.seconds),
        tracktion::TimePosition::fromSeconds(visible_range.end.seconds));
    m_thumbnail.drawChannels(g, bounds, tracktion_visible_range, vertical_zoom);
    return true;
}

} // namespace rock_hero::common::audio
