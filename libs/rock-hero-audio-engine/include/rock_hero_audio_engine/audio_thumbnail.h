/*!
\file audio_thumbnail.h
\brief Tracktion-free audio thumbnail wrapper for use by UI components.
*/

#pragma once

#include <filesystem>
#include <memory>

// Forward declarations; callers need not include Tracktion headers.
// The inline keyword must match Tracktion's own declaration to avoid C2049 on MSVC.
namespace tracktion
{
inline namespace engine
{
class Engine;
} // namespace engine
} // namespace tracktion

// Forward declarations — full definitions provided by juce_gui_basics, which consuming
// translation units (rock-hero-ui) already link.
namespace juce
{
class Component;
class Graphics;
template <typename ValueType> class Rectangle;
} // namespace juce

namespace rock_hero
{

/*!
\brief Owns audio thumbnail proxy generation and drawing, hiding Tracktion types from consumers.

Wraps tracktion::SmartThumbnail behind a Tracktion-free public interface so that UI code can
render audio thumbnails without including Tracktion headers or linking Tracktion libraries.

Created via AudioEngine::createThumbnail() rather than constructed directly, so that the
Tracktion Engine reference never leaks into calling code.

\see AudioEngine
*/
class AudioThumbnail
{
public:
    /*!
    \brief Creates the thumbnail bound to the given Tracktion Engine instance.

    Prefer AudioEngine::createThumbnail() over calling this constructor directly.

    \param engine The Tracktion Engine instance used for proxy generation.
    \param owner The component that should be repainted when the proxy is ready.
    */
    AudioThumbnail(tracktion::Engine& engine, juce::Component& owner);

    /*!
    \brief Destroys the thumbnail and releases proxy resources.
    */
    ~AudioThumbnail();

    AudioThumbnail(const AudioThumbnail&) = delete;
    AudioThumbnail& operator=(const AudioThumbnail&) = delete;
    AudioThumbnail(AudioThumbnail&&) = delete;
    AudioThumbnail& operator=(AudioThumbnail&&) = delete;

    /*!
    \brief Sets the audio file whose thumbnail should be displayed.

    Begins asynchronous proxy generation. Use isGeneratingProxy() and getProxyProgress() to
    track progress.

    \param file Path to the audio file.
    */
    void setFile(const std::filesystem::path& file);

    /*!
    \brief Reports whether the thumbnail proxy is still being generated.
    \return True while the proxy is being built.
    */
    [[nodiscard]] bool isGeneratingProxy() const;

    /*!
    \brief Returns the proxy generation progress as a fraction in [0, 1].
    \return Progress fraction, or 0 if no file is set.
    */
    [[nodiscard]] float getProxyProgress() const;

    /*!
    \brief Returns the duration of the loaded audio file in seconds.
    \return Duration in seconds, or 0 if no file is set.
    */
    [[nodiscard]] double getLength() const;

    /*!
    \brief Draws the audio channels into the given graphics context.

    Renders the full duration of the loaded file into the specified bounds.

    \param g Graphics context to draw into.
    \param bounds Rectangle defining the drawing area.
    \param vertical_zoom Vertical scale factor (1.0 = normal).
    */
    void drawChannels(juce::Graphics& g, juce::Rectangle<int> bounds, float vertical_zoom) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero
