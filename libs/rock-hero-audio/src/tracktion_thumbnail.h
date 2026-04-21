/*!
\file tracktion_thumbnail.h
\brief Tracktion-backed concrete implementation of the Thumbnail interface.
*/

#pragma once

#include <rock_hero/audio/thumbnail.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::audio
{

/*!
\brief Tracktion-backed adapter that satisfies the Thumbnail interface.

Wraps tracktion::SmartThumbnail so that consumers of the Thumbnail port do not see Tracktion
types. Visible only inside rock-hero-audio; callers obtain instances through
Engine::createThumbnail().

\see Thumbnail
\see Engine
*/
class TracktionThumbnail final : public Thumbnail
{
public:
    /*!
    \brief Creates the thumbnail bound to the given Tracktion Engine instance.

    \param engine The Tracktion Engine instance used for proxy generation.
    \param owner The component that should be repainted when the proxy is ready.
    */
    TracktionThumbnail(tracktion::Engine& engine, juce::Component& owner);

    /*! \copydoc Thumbnail::setFile */
    void setFile(const juce::File& file) override;

    /*! \copydoc Thumbnail::isGeneratingProxy */
    [[nodiscard]] bool isGeneratingProxy() const override;

    /*! \copydoc Thumbnail::getProxyProgress */
    [[nodiscard]] float getProxyProgress() const override;

    /*! \copydoc Thumbnail::getLength */
    [[nodiscard]] double getLength() const override;

    /*! \copydoc Thumbnail::drawChannels */
    void drawChannels(juce::Graphics& g, juce::Rectangle<int> bounds, float vertical_zoom) override;

private:
    // Tracktion Engine instance used to resolve AudioFile metadata and proxy generation.
    tracktion::Engine& m_engine;

    // Tracktion's waveform proxy; owns the async proxy-generation state and renders the cached
    // audio data during drawChannels().
    tracktion::SmartThumbnail m_thumbnail;

    // Cached duration used by UI coordinate mapping without exposing Tracktion types.
    double m_total_length_seconds{0.0};
};

} // namespace rock_hero::audio
