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

// Minimal fake audio-edit port that records the latest track-audio provision request.
struct FakeEdit final : IEdit
{
    // Seeds the fake with the accepted track audio returned from provisionTrackAudio().
    explicit FakeEdit(std::optional<core::TrackAudio> provision_result)
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

    // Records the requested track and asset for boundary-contract checks.
    std::optional<core::TrackAudio> provisionTrackAudio(
        core::TrackId track_id, const core::AudioAsset& audio_asset) override
    {
        last_track_id = track_id;
        last_audio_asset = audio_asset;
        ++call_count;
        return result;
    }

    // Result returned from provisionTrackAudio() to simulate backend success or failure.
    std::optional<core::TrackAudio> result{};

    // Result returned from provisionTrack() to simulate backend success or failure.
    bool next_provision_track_result{true};

    // Last track id received through provisionTrack(), if the fake has been called.
    std::optional<core::TrackId> last_provisioned_track_id{};

    // Last track name received through provisionTrack(), if the fake has been called.
    std::optional<std::string> last_provisioned_track_name{};

    // Last track id received through provisionTrackAudio(), if the fake has been called.
    std::optional<core::TrackId> last_track_id{};

    // Last audio asset received through provisionTrackAudio(), if the fake has been called.
    std::optional<core::AudioAsset> last_audio_asset{};

    // Number of track-audio provision attempts observed by the fake.
    int call_count{0};

    // Number of backend-track-provision attempts observed by the fake.
    int provision_track_call_count{0};
};

// Builds the accepted track-audio value returned by the fake edit port.
[[nodiscard]] core::TrackAudio makeTrackAudio(std::filesystem::path path)
{
    return core::TrackAudio{
        .asset = core::AudioAsset{std::move(path)},
        .duration = core::TimeDuration{4.0},
    };
}

} // namespace

// Verifies the audio-edit port receives the full backend track-provision request.
TEST_CASE("IEdit fake receives a track provision request", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};

    const auto provisioned = edit.provisionTrack(core::TrackId{7}, "Full Mix");

    CHECK(provisioned == std::optional{core::TrackSpec{.name = "Full Mix"}});
    CHECK(edit.provision_track_call_count == 1);
    CHECK(edit.last_provisioned_track_id == std::optional{core::TrackId{7}});
    CHECK(edit.last_provisioned_track_name == std::optional<std::string>{"Full Mix"});
}

// Verifies the port return value can represent failed track provisioning.
TEST_CASE("IEdit fake can report failed track provisioning", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};
    edit.next_provision_track_result = false;

    const auto provisioned = edit.provisionTrack(core::TrackId{9}, "Rejected");

    CHECK_FALSE(provisioned.has_value());
    CHECK(edit.provision_track_call_count == 1);
    CHECK(edit.last_provisioned_track_id == std::optional{core::TrackId{9}});
    CHECK(edit.last_provisioned_track_name == std::optional<std::string>{"Rejected"});
}

// Verifies the audio-edit port receives the full track-audio provision request.
TEST_CASE("IEdit fake receives a track audio provision request", "[audio][edit]")
{
    const core::TrackAudio audio = makeTrackAudio(std::filesystem::path{"drums.wav"});
    FakeEdit edit{audio};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto provisioned_audio = edit.provisionTrackAudio(core::TrackId{3}, asset);

    CHECK(provisioned_audio == std::optional{audio});
    CHECK(edit.call_count == 1);
    CHECK(edit.last_track_id == std::optional{core::TrackId{3}});
    CHECK(edit.last_audio_asset == std::optional{asset});
}

// Verifies the port return value can represent failed track-audio provisioning.
TEST_CASE("IEdit fake can report failed track audio provisioning", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};
    const core::AudioAsset asset{std::filesystem::path{"missing.wav"}};

    const auto provisioned_audio = edit.provisionTrackAudio(core::TrackId{1}, asset);

    CHECK_FALSE(provisioned_audio.has_value());
    CHECK(edit.call_count == 1);
}

} // namespace rock_hero::audio
