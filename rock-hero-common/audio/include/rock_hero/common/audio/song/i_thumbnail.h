/*!
\file i_thumbnail.h
\brief Tracktion-free audio thumbnail interface for use by UI components.
*/

#pragma once

#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/timeline/timeline.h>

// Forward declarations; full definitions provided by juce_gui_basics, which consuming
// translation units (rock-hero-editor/ui) already link.
namespace juce
{
// Forward declaration for draw calls supplied by JUCE UI components.
class Graphics;

// Forward declaration for draw bounds used by thumbnail rendering.
template <typename ValueType> class Rectangle;
} // namespace juce

namespace rock_hero::common::audio
{

/*!
\brief Abstract audio thumbnail interface that hides Tracktion types from consumers.

Exposes only JUCE-facing file and graphics types so that UI code can render audio thumbnails
without including Tracktion headers or linking Tracktion libraries. Thumbnail source assignment
uses the project-owned common::core::AudioAsset value so UI code stays framework-free at the loading
boundary. Concrete instances are obtained through IThumbnailFactory::createThumbnail().

\see IThumbnailFactory
*/
class IThumbnail
{
public:
    /*! \brief Destroys the thumbnail and releases proxy resources. */
    virtual ~IThumbnail() = default;

    /*!
    \brief Sets the audio asset whose thumbnail should be displayed.

    Begins asynchronous proxy generation. Use isGeneratingProxy() and getProxyProgress() to
    track progress.

    \param audio_asset Framework-free audio asset reference to display.
    */
    virtual void setSource(const common::core::AudioAsset& audio_asset) = 0;

    /*!
    \brief Reports whether a drawable source asset is currently loaded.
    \return True when source data is available for drawing.
    */
    [[nodiscard]] virtual bool hasSource() const = 0;

    /*!
    \brief Reports whether the thumbnail proxy is still being generated.
    \return True while the proxy is being built.
    */
    [[nodiscard]] virtual bool isGeneratingProxy() const = 0;

    /*!
    \brief Returns the proxy generation progress as a fraction in [0, 1].
    \return Progress fraction, or 0 if no file is set.
    */
    [[nodiscard]] virtual float getProxyProgress() const = 0;

    /*!
    \brief Draws the audio channels into the given graphics context.

    Renders the requested visible range of the loaded asset into the specified bounds. Invalid
    visible ranges fail without drawing.

    \param g Graphics context to draw into.
    \param bounds Rectangle defining the drawing area.
    \param visible_range Range inside the loaded source asset that is currently visible.
    \param vertical_zoom Vertical scale factor (1.0 = normal).
    \return True when the range was valid and drawing was attempted.
    */
    [[nodiscard]] virtual bool drawChannels(
        juce::Graphics& g, juce::Rectangle<int> bounds, common::core::TimeRange visible_range,
        float vertical_zoom) = 0;

protected:
    /*! \brief Allows only derived thumbnail adapters to construct the interface base. */
    IThumbnail() = default;

    /*! \brief Allows derived adapters to copy the interface base when needed. */
    IThumbnail(const IThumbnail&) = default;

    /*!
    \brief Allows derived adapters to copy-assign the interface base when needed.
    \return Reference to this thumbnail base.
    */
    IThumbnail& operator=(const IThumbnail&) = default;

    /*! \brief Allows derived adapters to move the interface base when needed. */
    IThumbnail(IThumbnail&&) = default;

    /*!
    \brief Allows derived adapters to move-assign the interface base when needed.
    \return Reference to this thumbnail base.
    */
    IThumbnail& operator=(IThumbnail&&) = default;
};

} // namespace rock_hero::common::audio
