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

// Configurable edit fake that accepts framework-free values for coordinator commit tests.
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

    std::optional<core::TrackAudio> provisionTrackAudio(
        core::TrackId track_id, const core::AudioAsset& audio_asset) override
    {
        last_track_id = track_id;
        last_audio_asset = audio_asset;
        ++provision_track_audio_call_count;

        if (!next_provision_track_audio_result)
        {
            return std::nullopt;
        }

        return core::TrackAudio{
            .asset = audio_asset,
            .duration = core::TimeDuration{4.0},
        };
    }

    bool next_provision_track_result{true};
    bool next_provision_track_audio_result{true};
    int provision_track_call_count{0};
    int provision_track_audio_call_count{0};
    std::optional<core::TrackId> last_provisioned_track_id{};
    std::optional<std::string> last_provisioned_track_name{};
    std::optional<core::TrackId> last_track_id{};
    std::optional<core::AudioAsset> last_audio_asset{};
};

// Reads track audio by value so tests can inspect committed state safely.
[[nodiscard]] std::optional<core::TrackAudio> findTrackAudio(
    const core::Session& session, core::TrackId track_id)
{
    const core::Track* const track = session.findTrack(track_id);
    if (track == nullptr)
    {
        return std::nullopt;
    }

    return track->audio;
}

// Builds the accepted audio value expected after a successful coordinator transaction.
[[nodiscard]] core::TrackAudio makeTrackAudio(std::filesystem::path path)
{
    return core::TrackAudio{
        .asset = core::AudioAsset{std::move(path)},
        .duration = core::TimeDuration{4.0},
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
    CHECK_FALSE(session.tracks()[0].audio.has_value());
    CHECK(edit.provision_track_audio_call_count == 0);
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

// Verifies the coordinator asks the backend and commits the accepted track audio.
TEST_CASE("EditCoordinator stores accepted track audio", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    const core::TrackId track_id = coordinator.createTrack("Full Mix");
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    const bool audio_set = coordinator.setTrackAudio(track_id, audio_asset);

    CHECK(audio_set);
    CHECK(edit.provision_track_audio_call_count == 1);
    CHECK(edit.last_track_id == std::optional{track_id});
    CHECK(edit.last_audio_asset == std::optional{audio_asset});
    CHECK(
        findTrackAudio(coordinator.session(), track_id) ==
        std::optional{makeTrackAudio(std::filesystem::path{"mix.wav"})});
}

// Verifies backend rejection leaves the session unchanged and later recovery is possible.
TEST_CASE("EditCoordinator preserves session on audio failure", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    edit.next_provision_track_audio_result = false;
    EditCoordinator coordinator{edit};
    const core::TrackId track_id = coordinator.createTrack("Full Mix");
    const core::AudioAsset failed_asset{std::filesystem::path{"missing.wav"}};

    const bool failed = coordinator.setTrackAudio(track_id, failed_asset);

    CHECK_FALSE(failed);
    CHECK(findTrackAudio(coordinator.session(), track_id) == std::nullopt);

    edit.next_provision_track_audio_result = true;
    const core::AudioAsset accepted_asset{std::filesystem::path{"mix.wav"}};
    const bool accepted = coordinator.setTrackAudio(track_id, accepted_asset);

    CHECK(accepted);
    CHECK(
        findTrackAudio(coordinator.session(), track_id) ==
        std::optional{makeTrackAudio(std::filesystem::path{"mix.wav"})});
}

// Verifies missing tracks are rejected before the backend can mutate playback state.
TEST_CASE("EditCoordinator ignores missing tracks", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    const bool audio_set = coordinator.setTrackAudio(core::TrackId{9}, audio_asset);

    CHECK_FALSE(audio_set);
    CHECK(edit.provision_track_audio_call_count == 0);
}

} // namespace rock_hero::ui
