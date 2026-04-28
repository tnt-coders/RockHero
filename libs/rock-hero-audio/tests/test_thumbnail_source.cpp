#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <type_traits>

namespace rock_hero::audio
{

namespace
{

// Exercises the UI-facing IThumbnail contract without constructing a concrete JUCE/Tracktion
// adapter.
void assignThumbnailSource(IThumbnail& thumbnail, const core::AudioAsset& audio_asset)
{
    thumbnail.setSource(audio_asset);
}

} // namespace

// Verifies the public IThumbnail surface accepts the project-owned asset value directly.
TEST_CASE("IThumbnail source assignment uses AudioAsset", "[audio][thumbnail]")
{
    static_assert(std::is_same_v<
                  decltype(&IThumbnail::setSource),
                  void (IThumbnail::*)(const core::AudioAsset&)>);
    static_assert(std::is_same_v<
                  decltype(&assignThumbnailSource),
                  void (*)(IThumbnail&, const core::AudioAsset&)>);

    SUCCEED();
}

// Verifies IThumbnail creation is exposed through a narrow audio-owned factory port.
TEST_CASE("IThumbnailFactory creates IThumbnail adapters", "[audio][thumbnail]")
{
    static_assert(std::is_same_v<
                  decltype(&IThumbnailFactory::createThumbnail),
                  std::unique_ptr<IThumbnail> (IThumbnailFactory::*)(juce::Component&)>);

    SUCCEED();
}

} // namespace rock_hero::audio
