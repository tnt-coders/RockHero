/*!
\file thumbnail_creator.h
\brief Callback type used by editor views to create audio thumbnails.
*/

#pragma once

#include <functional>
#include <memory>

namespace juce
{
// Forward declaration keeps the callback header light while still naming the view owner.
class Component;
} // namespace juce

namespace rock_hero::audio
{
// Forward declaration of the thumbnail interface created by the callback.
class Thumbnail;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

/*!
\brief Creates a thumbnail owned by a view component.

The callback receives the component that will own and repaint for the thumbnail. Production code
typically adapts this to audio::Engine::createThumbnail(...). The callback is consumed during view
construction and is not retained.
*/
using ThumbnailCreator = std::function<std::unique_ptr<audio::Thumbnail>(juce::Component& owner)>;

} // namespace rock_hero::ui
