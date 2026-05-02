#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <utility>

namespace rock_hero::audio
{

namespace
{

// Minimal fake audio-edit port that records the latest asset-load request.
struct FakeEdit final : IEdit
{
    // Seeds the fake with the accepted clip returned from loadAudioAsset().
    explicit FakeEdit(std::optional<core::AudioClip> load_result)
        : result{std::move(load_result)}
    {}

    // Records the requested track, asset, and position so tests can inspect the boundary call.
    std::optional<core::AudioClip> loadAudioAsset(
        core::TrackId track_id, const core::AudioAsset& audio_asset,
        core::TimePosition position) override
    {
        last_track_id = track_id;
        last_audio_asset = audio_asset;
        last_position = position;
        ++call_count;
        return result;
    }

    // Result returned from loadAudioAsset() to simulate backend success or failure.
    std::optional<core::AudioClip> result{};

    // Last track id received through loadAudioAsset(), if the fake has been called.
    std::optional<core::TrackId> last_track_id{};

    // Last audio asset received through loadAudioAsset(), if the fake has been called.
    std::optional<core::AudioAsset> last_audio_asset{};

    // Last timeline position received through loadAudioAsset(), if the fake has been called.
    std::optional<core::TimePosition> last_position{};

    // Number of asset-load attempts observed by the fake.
    int call_count{0};
};

// Builds the accepted clip value returned by the fake edit port.
[[nodiscard]] core::AudioClip makeAudioClip(std::filesystem::path path)
{
    return core::AudioClip{
        .id = core::AudioClipId{},
        .asset = core::AudioAsset{std::move(path)},
        .asset_duration = core::TimeDuration{4.0},
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{4.0},
            },
        .position = core::TimePosition{},
    };
}

} // namespace

// Verifies the audio-edit port receives the session track identity.
TEST_CASE("IEdit fake receives a track id", "[audio][edit]")
{
    const core::AudioClip clip = makeAudioClip(std::filesystem::path{"guitar.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"guitar.wav"}};

    const auto loaded_clip = edit.loadAudioAsset(core::TrackId{7}, asset, core::TimePosition{});

    CHECK(loaded_clip == std::optional<core::AudioClip>{clip});
    CHECK(edit.last_track_id == std::optional<core::TrackId>{core::TrackId{7}});
}

// Verifies the audio-edit port receives the framework-free asset reference.
TEST_CASE("IEdit fake receives an audio asset", "[audio][edit]")
{
    const core::AudioClip clip = makeAudioClip(std::filesystem::path{"drums.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto loaded_clip = edit.loadAudioAsset(core::TrackId{3}, asset, core::TimePosition{});

    CHECK(loaded_clip == std::optional<core::AudioClip>{clip});
    CHECK(edit.last_audio_asset == std::optional<core::AudioAsset>{asset});
}

// Verifies the audio-edit port receives the requested timeline position.
TEST_CASE("IEdit fake receives a timeline position", "[audio][edit]")
{
    const core::AudioClip clip = makeAudioClip(std::filesystem::path{"loop.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"loop.wav"}};

    const auto loaded_clip = edit.loadAudioAsset(core::TrackId{1}, asset, core::TimePosition{2.0});

    CHECK(loaded_clip == std::optional<core::AudioClip>{clip});
    CHECK(edit.last_position == std::optional<core::TimePosition>{core::TimePosition{2.0}});
}

// Verifies the port return value can represent failed asset loads.
TEST_CASE("IEdit fake can report failed asset loads", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};
    const core::AudioAsset asset{std::filesystem::path{"missing.wav"}};

    const auto loaded_clip = edit.loadAudioAsset(core::TrackId{1}, asset, core::TimePosition{});

    CHECK_FALSE(loaded_clip.has_value());
    CHECK(edit.call_count == 1);
}

} // namespace rock_hero::audio
