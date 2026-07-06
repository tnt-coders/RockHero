#include "tracktion/engine_behaviors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

namespace rock_hero::common::audio
{

// Waveform thumbnails must stay drawable from stored data at the timeline's deepest zoom:
// juce::AudioThumbnail falls back to reading the audio file directly past the stored
// granularity, and that path draws nothing for Tracktion's reader-fed thumbnails once a cached
// thumbnail is restored (the reader is gone and cannot be recreated). The stored granularity is
// serialized directly after the "jatm" magic, so the invariant is checked through the format.
TEST_CASE("UI behaviour builds fine-grained waveform thumbnails", "[audio][engine-behaviors]")
{
    juce::AudioFormatManager format_manager;
    juce::AudioThumbnailCache cache{4};
    RockHeroUIBehavior behavior{[](PluginWindowCommand) {}};

    const std::unique_ptr<juce::AudioThumbnailBase> thumbnail =
        behavior.createAudioThumbnail(256, format_manager, cache);
    REQUIRE(thumbnail != nullptr);

    thumbnail->reset(2, 44100.0, 0);
    juce::MemoryOutputStream stream;
    thumbnail->saveTo(stream);
    REQUIRE(stream.getDataSize() >= 8);

    juce::MemoryInputStream input{stream.getData(), stream.getDataSize(), false};
    std::array<char, 4> magic{};
    REQUIRE(input.read(magic.data(), static_cast<int>(magic.size())) == 4);
    CHECK(std::string(magic.data(), magic.size()) == "jatm");

    // The stored-data draw path stays in use while pixels-per-second is at most
    // sample_rate / granularity; the timeline zoom caps at 1264 px/s, and 22.05kHz is the
    // lowest sample rate a real backing track uses.
    const int samples_per_point = input.readInt();
    CHECK(samples_per_point >= 1);
    CHECK(samples_per_point * 1264 <= 22050);
}

} // namespace rock_hero::common::audio
