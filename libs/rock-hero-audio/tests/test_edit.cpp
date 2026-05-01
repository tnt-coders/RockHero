#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/audio/i_edit.h>

namespace rock_hero::audio
{

namespace
{

// Minimal fake audio-edit port that records the latest clip-setting request.
struct FakeEdit final : IEdit
{
    // Seeds the fake with results returned by the edit probe and clip-setting methods.
    FakeEdit(std::optional<core::TimeDuration> duration_result, bool set_result)
        : duration{duration_result}
        , result{set_result}
    {}

    // Records the asset duration probe so controller tests can inspect the boundary call.
    std::optional<core::TimeDuration> readAudioAssetDuration(
        const core::AudioAsset& audio_asset) const override
    {
        last_duration_probe_asset = audio_asset;
        ++duration_call_count;
        return duration;
    }

    // Records the requested track and clip so controller tests can inspect the boundary call.
    bool setTrackAudioClip(core::TrackId track_id, const core::AudioClip& audio_clip) override
    {
        last_track_id = track_id;
        last_audio_clip = audio_clip;
        ++call_count;
        return result;
    }

    // Result returned from readAudioAssetDuration() to simulate probe success or failure.
    std::optional<core::TimeDuration> duration{};

    // Result returned from setTrackAudioClip() to simulate backend success or failure.
    bool result{true};

    // Last track id received through setTrackAudioClip(), if the fake has been called.
    std::optional<core::TrackId> last_track_id{};

    // Last audio asset received through readAudioAssetDuration(), if the fake has been called.
    mutable std::optional<core::AudioAsset> last_duration_probe_asset{};

    // Last audio clip received through setTrackAudioClip(), if the fake has been called.
    std::optional<core::AudioClip> last_audio_clip{};

    // Number of duration-probe attempts observed by the fake.
    mutable int duration_call_count{0};

    // Number of clip-setting attempts observed by the fake.
    int call_count{0};
};

} // namespace

// Verifies the audio-edit port can probe audio asset duration separately from mutation.
TEST_CASE("IEdit fake probes audio asset duration", "[audio][edit]")
{
    FakeEdit edit{core::TimeDuration{4.0}, true};
    const core::AudioAsset asset{std::filesystem::path{"guitar.wav"}};

    const auto duration = edit.readAudioAssetDuration(asset);

    CHECK(duration == std::optional<core::TimeDuration>{core::TimeDuration{4.0}});
    CHECK(edit.last_duration_probe_asset == std::optional<core::AudioAsset>{asset});
    CHECK(edit.duration_call_count == 1);
}

// Verifies the audio-edit port receives the session track identity.
TEST_CASE("IEdit fake receives a track id", "[audio][edit]")
{
    FakeEdit edit{core::TimeDuration{4.0}, true};
    const core::AudioClip clip{
        .id = core::AudioClipId{},
        .asset = core::AudioAsset{std::filesystem::path{"guitar.wav"}},
        .asset_duration = core::TimeDuration{4.0},
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{4.0},
            },
        .position = core::TimePosition{},
    };

    const bool applied = edit.setTrackAudioClip(core::TrackId{7}, clip);

    CHECK(applied);
    REQUIRE(edit.last_track_id.has_value());
    CHECK(edit.last_track_id == core::TrackId{7});
}

// Verifies the audio-edit port receives the framework-free clip.
TEST_CASE("IEdit fake receives an audio clip", "[audio][edit]")
{
    FakeEdit edit{core::TimeDuration{4.0}, true};
    const core::AudioClip clip{
        .id = core::AudioClipId{},
        .asset = core::AudioAsset{std::filesystem::path{"drums.wav"}},
        .asset_duration = core::TimeDuration{4.0},
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{4.0},
            },
        .position = core::TimePosition{},
    };

    const bool applied = edit.setTrackAudioClip(core::TrackId{3}, clip);

    CHECK(applied);
    REQUIRE(edit.last_audio_clip.has_value());
    CHECK(edit.last_audio_clip == std::optional<core::AudioClip>{clip});
}

// Verifies the port return value can represent failed clip changes.
TEST_CASE("IEdit fake can report failed clip changes", "[audio][edit]")
{
    FakeEdit edit{core::TimeDuration{4.0}, false};
    const core::AudioClip clip{
        .id = core::AudioClipId{},
        .asset = core::AudioAsset{std::filesystem::path{"missing.wav"}},
        .asset_duration = core::TimeDuration{4.0},
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{4.0},
            },
        .position = core::TimePosition{},
    };

    const bool applied = edit.setTrackAudioClip(core::TrackId{1}, clip);

    CHECK_FALSE(applied);
    CHECK(edit.call_count == 1);
}

} // namespace rock_hero::audio
