/*!
\file tracktion_thumbnail.h
\brief Tracktion-backed concrete implementation of the IThumbnail interface.
*/

#pragma once

#include <rock_hero/common/audio/song/i_thumbnail.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

/*!
\brief Tracktion-backed adapter that satisfies the IThumbnail interface.

Wraps tracktion::SmartThumbnail so that consumers of the IThumbnail port do not see Tracktion
types. Visible only inside the common/audio implementation; callers obtain instances through the
IThumbnailFactory port.

\see IThumbnail
\see IThumbnailFactory
*/
class TracktionThumbnail final : public IThumbnail
{
public:
    /*!
    \brief Creates the thumbnail bound to the given Tracktion Engine instance.

    \param engine The Tracktion Engine instance used for proxy generation.
    \param owner The component that should be repainted when the proxy is ready.
    */
    TracktionThumbnail(tracktion::Engine& engine, juce::Component& owner);

    /*! \copydoc IThumbnail::setSource */
    void setSource(const common::core::AudioAsset& audio_asset) override;

    /*! \copydoc IThumbnail::hasSource */
    [[nodiscard]] bool hasSource() const override;

    /*! \copydoc IThumbnail::isGeneratingProxy */
    [[nodiscard]] bool isGeneratingProxy() const override;

    /*! \copydoc IThumbnail::getProxyProgress */
    [[nodiscard]] float getProxyProgress() const override;

    /*! \copydoc IThumbnail::drawChannels */
    [[nodiscard]] bool drawChannels(
        juce::Graphics& g, juce::Rectangle<int> bounds, common::core::TimeRange visible_range,
        float vertical_zoom) override;

private:
    // Tracktion Engine instance used to resolve AudioFile metadata and proxy generation.
    tracktion::Engine& m_engine;

    // Tracktion's waveform proxy; owns the async proxy-generation state and renders the cached
    // audio data during drawChannels().
    tracktion::SmartThumbnail m_thumbnail;

    // Tracks whether the last source assignment resolved to drawable source data.
    bool m_has_source{false};

    // Cached source duration used to reject invalid draw ranges before calling Tracktion.
    double m_source_length_seconds{0.0};

    // Path of the currently loaded source. Tracktion identifies audio files for thumbnail
    // caching purely by full path hash, so calling setNewFile with the same path is a no-op
    // even if the bytes on disk changed. We compare against this to know when to evict the
    // cached thumbnail entries through AudioFileManager::callListenersOnMessageThread.
    juce::File m_current_source_file{};
};

} // namespace rock_hero::common::audio
