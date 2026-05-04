#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/core/session.h>
#include <rock_hero/ui/edit_coordinator.h>
#include <string>
#include <utility>

namespace rock_hero::ui
{

namespace
{

// Configurable edit fake that accepts identity-free values for coordinator commit tests.
class FakeEdit final : public audio::IEdit
{
public:
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

    std::optional<core::AudioClipSpec> provisionAudioClip(
        core::TrackId track_id, core::AudioClipId audio_clip_id,
        const core::AudioAsset& audio_asset, core::TimePosition position) override
    {
        last_track_id = track_id;
        last_audio_clip_id = audio_clip_id;
        last_audio_asset = audio_asset;
        last_position = position;
        ++provision_audio_clip_call_count;

        if (!next_provision_audio_clip_result)
        {
            return std::nullopt;
        }

        return core::AudioClipSpec{
            .asset = audio_asset,
            .asset_duration = core::TimeDuration{4.0},
            .source_range =
                core::TimeRange{
                    .start = core::TimePosition{},
                    .end = core::TimePosition{4.0},
                },
            .position = position,
        };
    }

    bool next_provision_track_result{true};
    bool next_provision_audio_clip_result{true};
    int provision_track_call_count{0};
    int provision_audio_clip_call_count{0};
    std::optional<core::TrackId> last_provisioned_track_id{};
    std::optional<std::string> last_provisioned_track_name{};
    std::optional<core::TrackId> last_track_id{};
    std::optional<core::AudioClipId> last_audio_clip_id{};
    std::optional<core::AudioAsset> last_audio_asset{};
    std::optional<core::TimePosition> last_position{};
};

// Reads a track clip by value so tests can inspect committed state safely.
[[nodiscard]] std::optional<core::AudioClip> findTrackAudioClip(
    const core::Session& session, core::TrackId track_id)
{
    const core::Track* const track = session.findTrack(track_id);
    if (track == nullptr)
    {
        return std::nullopt;
    }

    return track->audio_clip;
}

// Builds the accepted clip value expected after a successful coordinator transaction.
[[nodiscard]] core::AudioClip makeAudioClip(
    core::AudioClipId id, std::filesystem::path path, core::TimePosition position = {})
{
    return core::AudioClip{
        .id = id,
        .asset = core::AudioAsset{std::move(path)},
        .asset_duration = core::TimeDuration{4.0},
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{4.0},
            },
        .position = position,
    };
}

} // namespace

// Verifies the coordinator owns editor-facing track creation for the app workflow.
TEST_CASE("EditCoordinator creates a session track", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};

    const core::TrackId track_id = coordinator.createTrack("Full Mix");
    const core::Session& session = coordinator.session();

    CHECK(track_id == core::TrackId{1});
    CHECK(edit.provision_track_call_count == 1);
    CHECK(edit.last_provisioned_track_id == std::optional{track_id});
    CHECK(edit.last_provisioned_track_name == std::optional<std::string>{"Full Mix"});
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].id == track_id);
    CHECK(session.tracks()[0].name == "Full Mix");
    CHECK_FALSE(session.tracks()[0].audio_clip.has_value());
    CHECK(edit.provision_audio_clip_call_count == 0);
}

// Verifies backend track-provision rejection leaves Session unchanged and consumes the id.
TEST_CASE("EditCoordinator preserves session on track failure", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    edit.next_provision_track_result = false;
    EditCoordinator coordinator{edit};

    const core::TrackId track_id = coordinator.createTrack("Full Mix");

    CHECK_FALSE(track_id.isValid());
    CHECK(coordinator.session().tracks().empty());
    CHECK(edit.provision_track_call_count == 1);

    edit.next_provision_track_result = true;
    const core::TrackId accepted_track_id = coordinator.createTrack("Stem");

    CHECK(accepted_track_id == core::TrackId{2});
    CHECK(edit.provision_track_call_count == 2);
    REQUIRE(coordinator.session().tracks().size() == 1);
    CHECK(coordinator.session().tracks()[0].id == accepted_track_id);
    CHECK(coordinator.session().tracks()[0].name == "Stem");
}

// Verifies the coordinator allocates identity, asks the backend, and commits the accepted clip.
TEST_CASE("EditCoordinator creates and stores an audio clip", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    const core::TrackId track_id = coordinator.createTrack("Full Mix");
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    const auto audio_clip_id =
        coordinator.createAudioClip(track_id, audio_asset, core::TimePosition{});

    CHECK(audio_clip_id == std::optional{core::AudioClipId{1}});
    CHECK(edit.provision_audio_clip_call_count == 1);
    CHECK(edit.last_track_id == std::optional{track_id});
    CHECK(edit.last_audio_clip_id == std::optional{core::AudioClipId{1}});
    CHECK(edit.last_audio_asset == std::optional{audio_asset});
    CHECK(edit.last_position == std::optional{core::TimePosition{}});
    CHECK(
        findTrackAudioClip(coordinator.session(), track_id) ==
        std::optional{makeAudioClip(core::AudioClipId{1}, std::filesystem::path{"mix.wav"})});
}

// Verifies backend rejection leaves the session unchanged while preserving id monotonicity.
TEST_CASE("EditCoordinator preserves session on backend failure", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    edit.next_provision_audio_clip_result = false;
    EditCoordinator coordinator{edit};
    const core::TrackId track_id = coordinator.createTrack("Full Mix");
    const core::AudioAsset failed_asset{std::filesystem::path{"missing.wav"}};

    const auto failed_clip_id =
        coordinator.createAudioClip(track_id, failed_asset, core::TimePosition{});

    CHECK_FALSE(failed_clip_id.has_value());
    CHECK(findTrackAudioClip(coordinator.session(), track_id) == std::nullopt);

    edit.next_provision_audio_clip_result = true;
    const core::AudioAsset accepted_asset{std::filesystem::path{"mix.wav"}};
    const auto accepted_clip_id =
        coordinator.createAudioClip(track_id, accepted_asset, core::TimePosition{});

    CHECK(accepted_clip_id == std::optional{core::AudioClipId{2}});
    CHECK(
        findTrackAudioClip(coordinator.session(), track_id) ==
        std::optional{makeAudioClip(core::AudioClipId{2}, std::filesystem::path{"mix.wav"})});
}

// Verifies missing tracks are rejected before the backend can mutate playback state.
TEST_CASE("EditCoordinator ignores missing tracks", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    const auto audio_clip_id =
        coordinator.createAudioClip(core::TrackId{9}, audio_asset, core::TimePosition{});

    CHECK_FALSE(audio_clip_id.has_value());
    CHECK(edit.provision_audio_clip_call_count == 0);
}

} // namespace rock_hero::ui
