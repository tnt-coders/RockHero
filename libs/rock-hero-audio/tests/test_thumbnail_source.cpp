#include <catch2/catch_test_macros.hpp>
#include <rock_hero/audio/thumbnail.h>
#include <type_traits>

namespace rock_hero::audio
{

namespace
{

// Exercises the UI-facing thumbnail contract without constructing a concrete JUCE/Tracktion
// adapter.
void assignThumbnailSource(Thumbnail& thumbnail, const core::AudioAsset& audio_asset)
{
    thumbnail.setSource(audio_asset);
}

} // namespace

// Verifies the public thumbnail surface accepts the project-owned asset value directly.
TEST_CASE("Thumbnail source assignment uses AudioAsset", "[audio][thumbnail]")
{
    static_assert(std::is_same_v<
                  decltype(&Thumbnail::setSource),
                  void (Thumbnail::*)(const core::AudioAsset&)>);
    static_assert(std::is_same_v<
                  decltype(&assignThumbnailSource),
                  void (*)(Thumbnail&, const core::AudioAsset&)>);

    SUCCEED();
}

} // namespace rock_hero::audio
