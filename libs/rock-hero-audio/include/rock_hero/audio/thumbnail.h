/*!
\file thumbnail.h
\brief Tracktion-free audio thumbnail interface for use by UI components.
*/

#pragma once

#include <filesystem>

// Forward declarations — full definitions provided by juce_gui_basics, which consuming
// translation units (rock-hero-ui) already link.
namespace juce
{
class Graphics;
template <typename ValueType> class Rectangle;
} // namespace juce

namespace rock_hero::audio
{

/*!
\brief Abstract audio thumbnail interface that hides Tracktion types from consumers.

Exposes only standard C++ and JUCE graphics types so that UI code can render audio thumbnails
without including Tracktion headers or linking Tracktion libraries. Concrete instances are
obtained from Engine::createThumbnail().

\see Engine
*/
class Thumbnail
{
public:
    /*! \brief Destroys the thumbnail and releases proxy resources. */
    virtual ~Thumbnail() = default;

    /*!
    \brief Sets the audio file whose thumbnail should be displayed.

    Begins asynchronous proxy generation. Use isGeneratingProxy() and getProxyProgress() to
    track progress.

    \param file Path to the audio file.
    */
    virtual void setFile(const std::filesystem::path& file) = 0;

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
    \brief Returns the duration of the loaded audio file in seconds.
    \return Duration in seconds, or 0 if no file is set.
    */
    [[nodiscard]] virtual double getLength() const = 0;

    /*!
    \brief Draws the audio channels into the given graphics context.

    Renders the full duration of the loaded file into the specified bounds.

    \param g Graphics context to draw into.
    \param bounds Rectangle defining the drawing area.
    \param vertical_zoom Vertical scale factor (1.0 = normal).
    */
    virtual void drawChannels(
        juce::Graphics& g, juce::Rectangle<int> bounds, float vertical_zoom) const = 0;

protected:
    Thumbnail() = default;
    Thumbnail(const Thumbnail&) = default;
    Thumbnail& operator=(const Thumbnail&) = default;
    Thumbnail(Thumbnail&&) = default;
    Thumbnail& operator=(Thumbnail&&) = default;
};

} // namespace rock_hero::audio
