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

// Minimal fake audio-edit port that records the latest clip-create request.
struct FakeEdit final : IEdit
{
    // Seeds the fake with the accepted clip returned from createAudioClip().
    explicit FakeEdit(std::optional<core::AudioClipData> create_result)
        : result{std::move(create_result)}
    {}

    // Records the requested backend track mapping.
    std::optional<core::TrackData> createTrack(
        core::TrackId track_id, const std::string& name) override
    {
        last_created_track_id = track_id;
        last_created_track_name = name;
        ++create_track_call_count;
        if (!next_create_track_result)
        {
            return std::nullopt;
        }

        return core::TrackData{
            .name = name,
        };
    }

    // Records the requested track, clip id, asset, and position for boundary-contract checks.
    std::optional<core::AudioClipData> createAudioClip(
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

    // Result returned from createAudioClip() to simulate backend success or failure.
    std::optional<core::AudioClipData> result{};

    // Result returned from createTrack() to simulate backend success or failure.
    bool next_create_track_result{true};

    // Last track id received through createTrack(), if the fake has been called.
    std::optional<core::TrackId> last_created_track_id{};

    // Last track name received through createTrack(), if the fake has been called.
    std::optional<std::string> last_created_track_name{};

    // Last track id received through createAudioClip(), if the fake has been called.
    std::optional<core::TrackId> last_track_id{};

    // Last audio clip id received through createAudioClip(), if the fake has been called.
    std::optional<core::AudioClipId> last_audio_clip_id{};

    // Last audio asset received through createAudioClip(), if the fake has been called.
    std::optional<core::AudioAsset> last_audio_asset{};

    // Last timeline position received through createAudioClip(), if the fake has been called.
    std::optional<core::TimePosition> last_position{};

    // Number of clip-create attempts observed by the fake.
    int call_count{0};

    // Number of backend-track-create attempts observed by the fake.
    int create_track_call_count{0};
};

// Builds the accepted clip value returned by the fake edit port.
[[nodiscard]] core::AudioClipData makeAudioClipData(std::filesystem::path path)
{
    return core::AudioClipData{
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
TEST_CASE("IEdit fake receives created track id", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};

    const auto created = edit.createTrack(core::TrackId{7}, "Full Mix");

    CHECK(created == std::optional<core::TrackData>{core::TrackData{.name = "Full Mix"}});
    CHECK(edit.last_created_track_id == std::optional<core::TrackId>{core::TrackId{7}});
}

// Verifies the audio-edit port receives the user-visible track name.
TEST_CASE("IEdit fake receives created track name", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};

    const auto created = edit.createTrack(core::TrackId{8}, "Guitar");

    CHECK(created == std::optional<core::TrackData>{core::TrackData{.name = "Guitar"}});
    CHECK(edit.last_created_track_name == std::optional<std::string>{"Guitar"});
}

// Verifies the audio-edit port receives the session track identity.
TEST_CASE("IEdit fake receives a track id", "[audio][edit]")
{
    const core::AudioClipData clip = makeAudioClipData(std::filesystem::path{"guitar.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"guitar.wav"}};

    const auto created_clip =
        edit.createAudioClip(core::TrackId{7}, core::AudioClipId{11}, asset, core::TimePosition{});

    CHECK(created_clip == std::optional<core::AudioClipData>{clip});
    CHECK(edit.last_track_id == std::optional<core::TrackId>{core::TrackId{7}});
}

// Verifies the audio-edit port receives the session-allocated audio clip identity.
TEST_CASE("IEdit fake receives an audio clip id", "[audio][edit]")
{
    const core::AudioClipData clip = makeAudioClipData(std::filesystem::path{"drums.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto created_clip =
        edit.createAudioClip(core::TrackId{3}, core::AudioClipId{12}, asset, core::TimePosition{});

    CHECK(created_clip == std::optional<core::AudioClipData>{clip});
    CHECK(edit.last_audio_clip_id == std::optional<core::AudioClipId>{core::AudioClipId{12}});
}

// Verifies the audio-edit port receives the framework-free asset reference.
TEST_CASE("IEdit fake receives an audio asset", "[audio][edit]")
{
    const core::AudioClipData clip = makeAudioClipData(std::filesystem::path{"drums.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto created_clip =
        edit.createAudioClip(core::TrackId{3}, core::AudioClipId{13}, asset, core::TimePosition{});

    CHECK(created_clip == std::optional<core::AudioClipData>{clip});
    CHECK(edit.last_audio_asset == std::optional<core::AudioAsset>{asset});
}

// Verifies the audio-edit port receives the requested timeline position.
TEST_CASE("IEdit fake receives a timeline position", "[audio][edit]")
{
    const core::AudioClipData clip = makeAudioClipData(std::filesystem::path{"loop.wav"});
    FakeEdit edit{clip};
    const core::AudioAsset asset{std::filesystem::path{"loop.wav"}};

    const auto created_clip = edit.createAudioClip(
        core::TrackId{1}, core::AudioClipId{14}, asset, core::TimePosition{2.0});

    CHECK(created_clip == std::optional<core::AudioClipData>{clip});
    CHECK(edit.last_position == std::optional<core::TimePosition>{core::TimePosition{2.0}});
}

// Verifies the port return value can represent failed clip creation.
TEST_CASE("IEdit fake can report failed clip creation", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};
    const core::AudioAsset asset{std::filesystem::path{"missing.wav"}};

    const auto created_clip =
        edit.createAudioClip(core::TrackId{1}, core::AudioClipId{15}, asset, core::TimePosition{});

    CHECK_FALSE(created_clip.has_value());
    CHECK(edit.call_count == 1);
}

} // namespace rock_hero::audio
