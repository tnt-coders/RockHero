/*!
\file i_thumbnail_factory.h
\brief Project-owned audio thumbnail factory interface.
*/

#pragma once

#include <memory>

namespace juce
{
// Forward declaration for UI components that own generated thumbnail adapters.
class Component;
} // namespace juce

namespace rock_hero::audio
{

// Forward declaration of the Tracktion-free thumbnail interface created by the factory.
class IThumbnail;

/*!
\brief Creates audio thumbnail adapters for UI-owned components.

This interface keeps thumbnail creation on the audio boundary without requiring UI code to depend
on the concrete Engine type. Implementations may use Tracktion or another backend internally, but
callers only receive the project-owned IThumbnail interface.
*/
class IThumbnailFactory
{
public:
    /*! \brief Destroys the factory interface. */
    virtual ~IThumbnailFactory() = default;

    /*!
    \brief Creates an IThumbnail bound to the supplied owner component.
    \param owner Component that should repaint when thumbnail proxy data changes.
    \return Newly created IThumbnail adapter.
    */
    [[nodiscard]] virtual std::unique_ptr<IThumbnail> createThumbnail(juce::Component& owner) = 0;

protected:
    /*! \brief Allows only concrete factories to construct the interface base. */
    IThumbnailFactory() = default;

    /*! \brief Allows derived factories to copy the interface base when needed. */
    IThumbnailFactory(const IThumbnailFactory&) = default;

    /*! \brief Allows derived factories to move the interface base when needed. */
    IThumbnailFactory(IThumbnailFactory&&) = default;

    /*!
    \brief Allows derived factories to copy-assign the interface base when needed.
    \return Reference to this factory base.
    */
    IThumbnailFactory& operator=(const IThumbnailFactory&) = default;

    /*!
    \brief Allows derived factories to move-assign the interface base when needed.
    \return Reference to this factory base.
    */
    IThumbnailFactory& operator=(IThumbnailFactory&&) = default;
};

} // namespace rock_hero::audio
