#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <string>
#include <utility>

namespace rock_hero::audio
{

namespace
{

// Minimal fake audio-edit port that records the latest clip-provision request.
struct FakeEdit final : IEdit
{
    // Seeds the fake with the accepted clip returned from provisionAudioClip().
    explicit FakeEdit(std::optional<core::AudioClipSpec> provision_result)
        : result{std::move(provision_result)}
    {}

    // Records the requested backend track mapping.
    std::optional<core::TrackSpec> provisionTrack(
        core::TrackId track_id, const std::string& name) override
    {
        last_provisioned_track_id = track_id;
        last_provisioned_track_name = name;
        ++provision_track_call_count;
        if (!next_provision_track_result)
        {
            return std::nullopt;
        }

        return core::TrackSpec{
            .name = name,
        };
    }

    // Records the requested track, clip id, asset, and position for boundary-contract checks.
    std::optional<core::AudioClipSpec> provisionAudioClip(
        core::TrackId track_id, core::AudioClipId audio_clip_id,
        const core::AudioAsset& audio_asset, core::TimePosition position) override
    {
        last_track_id = track_id;
        last_audio_clip_id = audio_clip_id;
        last_audio_asset = audio_asset;
        last_position = position;
        ++call_count;
        return result;
    }

    // Result returned from provisionAudioClip() to simulate backend success or failure.
    std::optional<core::AudioClipSpec> result{};

    // Result returned from provisionTrack() to simulate backend success or failure.
    bool next_provision_track_result{true};

    // Last track id received through provisionTrack(), if the fake has been called.
    std::optional<core::TrackId> last_provisioned_track_id{};

    // Last track name received through provisionTrack(), if the fake has been called.
    std::optional<std::string> last_provisioned_track_name{};

    // Last track id received through provisionAudioClip(), if the fake has been called.
    std::optional<core::TrackId> last_track_id{};

    // Last audio clip id received through provisionAudioClip(), if the fake has been called.
    std::optional<core::AudioClipId> last_audio_clip_id{};

    // Last audio asset received through provisionAudioClip(), if the fake has been called.
    std::optional<core::AudioAsset> last_audio_asset{};

    // Last timeline position received through provisionAudioClip(), if the fake has been called.
    std::optional<core::TimePosition> last_position{};

    // Number of clip-provision attempts observed by the fake.
    int call_count{0};

    // Number of backend-track-provision attempts observed by the fake.
    int provision_track_call_count{0};
};

// Builds the accepted clip value returned by the fake edit port.
[[nodiscard]] core::AudioClipSpec makeAudioClipSpec(std::filesystem::path path)
{
    return core::AudioClipSpec{
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

// Verifies the audio-edit port receives the session-allocated track identity.
TEST_CASE("IEdit fake receives provisioned track id", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};

    const auto provisioned = edit.provisionTrack(core::TrackId{7}, "Full Mix");

    CHECK(provisioned == std::optional<core::TrackSpec>{core::TrackSpec{.name = "Full Mix"}});
    CHECK(edit.last_provisioned_track_id == std::optional<core::TrackId>{core::TrackId{7}});
}

// Verifies the audio-edit port receives the user-visible track name.
TEST_CASE("IEdit fake receives provisioned track name", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};

    const auto provisioned = edit.provisionTrack(core::TrackId{8}, "Guitar");

    CHECK(provisioned == std::optional<core::TrackSpec>{core::TrackSpec{.name = "Guitar"}});
    CHECK(edit.last_provisioned_track_name == std::optional<std::string>{"Guitar"});
}

// Verifies the audio-edit port receives the session track identity.
TEST_CASE("IEdit fake receives a track id", "[audio][edit]")
{
    const core::AudioClipSpec clip = makeAudioClipSpec(std::filesystem::path{"guitar.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"guitar.wav"}};

    const auto provisioned_clip = edit.provisionAudioClip(
        core::TrackId{7}, core::AudioClipId{11}, asset, core::TimePosition{});

    CHECK(provisioned_clip == std::optional<core::AudioClipSpec>{clip});
    CHECK(edit.last_track_id == std::optional<core::TrackId>{core::TrackId{7}});
}

// Verifies the audio-edit port receives the session-allocated audio clip identity.
TEST_CASE("IEdit fake receives an audio clip id", "[audio][edit]")
{
    const core::AudioClipSpec clip = makeAudioClipSpec(std::filesystem::path{"drums.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto provisioned_clip = edit.provisionAudioClip(
        core::TrackId{3}, core::AudioClipId{12}, asset, core::TimePosition{});

    CHECK(provisioned_clip == std::optional<core::AudioClipSpec>{clip});
    CHECK(edit.last_audio_clip_id == std::optional<core::AudioClipId>{core::AudioClipId{12}});
}

// Verifies the audio-edit port receives the framework-free asset reference.
TEST_CASE("IEdit fake receives an audio asset", "[audio][edit]")
{
    const core::AudioClipSpec clip = makeAudioClipSpec(std::filesystem::path{"drums.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto provisioned_clip = edit.provisionAudioClip(
        core::TrackId{3}, core::AudioClipId{13}, asset, core::TimePosition{});

    CHECK(provisioned_clip == std::optional<core::AudioClipSpec>{clip});
    CHECK(edit.last_audio_asset == std::optional<core::AudioAsset>{asset});
}

// Verifies the audio-edit port receives the requested timeline position.
TEST_CASE("IEdit fake receives a timeline position", "[audio][edit]")
{
    const core::AudioClipSpec clip = makeAudioClipSpec(std::filesystem::path{"loop.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"loop.wav"}};

    const auto provisioned_clip = edit.provisionAudioClip(
        core::TrackId{1}, core::AudioClipId{14}, asset, core::TimePosition{2.0});

    CHECK(provisioned_clip == std::optional<core::AudioClipSpec>{clip});
    CHECK(edit.last_position == std::optional<core::TimePosition>{core::TimePosition{2.0}});
}

// Verifies the port return value can represent failed clip provisioning.
TEST_CASE("IEdit fake can report failed clip provisioning", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};
    const core::AudioAsset asset{std::filesystem::path{"missing.wav"}};

    const auto provisioned_clip = edit.provisionAudioClip(
        core::TrackId{1}, core::AudioClipId{15}, asset, core::TimePosition{});

    CHECK_FALSE(provisioned_clip.has_value());
    CHECK(edit.call_count == 1);
}

} // namespace rock_hero::audio
