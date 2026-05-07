#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/ui/edit_coordinator.h>
#include <utility>

namespace rock_hero::ui
{

namespace
{

// Fake edit port that accepts or rejects arrangement-audio loading deterministically.
class FakeEdit final : public audio::IEdit
{
public:
    // Records the selected asset and returns the configured duration.
    std::optional<core::TimeDuration> loadAudio(const core::AudioAsset& audio_asset) override
    {
        last_audio_asset = audio_asset;
        ++load_audio_call_count;
        return next_load_audio_result;
    }

    void clearAudio() override
    {
        ++clear_audio_call_count;
    }

    std::optional<core::TimeDuration> next_load_audio_result{core::TimeDuration{10.0}};
    int load_audio_call_count{0};
    int clear_audio_call_count{0};
    std::optional<core::AudioAsset> last_audio_asset{};
};

// Builds a song whose first arrangement references one audio asset.
[[nodiscard]] core::Song makeSong(std::filesystem::path audio_path)
{
    core::Song song;
    song.chart.arrangements.push_back(
        core::Arrangement{
            .part = core::Part::Lead,
            .difficulty = core::DifficultyRating{},
            .audio_asset = core::AudioAsset{std::move(audio_path)},
            .audio_duration = core::TimeDuration{},
            .tone_timeline_ref = {},
            .note_events = {},
        });
    return song;
}

} // namespace

// Verifies the coordinator exposes an empty session until loading succeeds.
TEST_CASE("EditCoordinator owns an empty session", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    const EditCoordinator coordinator{edit};

    CHECK(coordinator.session().arrangements().empty());
    CHECK(coordinator.session().currentArrangement() == nullptr);
    CHECK(edit.load_audio_call_count == 0);
}

// Verifies the coordinator asks the backend and commits accepted project audio.
TEST_CASE("EditCoordinator loads accepted song audio", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    const bool loaded = coordinator.loadSong(makeSong(audio_asset.path), 0);

    CHECK(loaded);
    CHECK(edit.load_audio_call_count == 1);
    CHECK(edit.last_audio_asset == std::optional{audio_asset});
    REQUIRE(coordinator.session().arrangements().size() == 1);
    const core::Arrangement& arrangement = coordinator.session().arrangements().front();
    CHECK(arrangement.audio_asset == std::optional{audio_asset});
    CHECK(arrangement.audio_duration == core::TimeDuration{10.0});
    CHECK(
        coordinator.session().timeline() == core::TimeRange{
                                                .start = core::TimePosition{},
                                                .end = core::TimePosition{10.0},
                                            });
}

// Verifies failed backend audio loading leaves the empty session unchanged.
TEST_CASE("EditCoordinator preserves session on project-audio failure", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    edit.next_load_audio_result = std::nullopt;
    const core::AudioAsset failed_asset{std::filesystem::path{"missing.wav"}};

    const bool failed = coordinator.loadSong(makeSong(failed_asset.path), 0);

    CHECK_FALSE(failed);
    CHECK(edit.load_audio_call_count == 1);
    CHECK(edit.last_audio_asset == std::optional{failed_asset});
    CHECK(coordinator.session().arrangements().empty());
    CHECK(coordinator.session().currentArrangement() == nullptr);
    CHECK(coordinator.session().timeline() == core::TimeRange{});
}

// Verifies non-positive durations do not become playable arrangement audio.
TEST_CASE("EditCoordinator rejects zero-duration audio", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    edit.next_load_audio_result = core::TimeDuration{};
    const core::AudioAsset failed_asset{std::filesystem::path{"empty.wav"}};

    const bool failed = coordinator.loadSong(makeSong(failed_asset.path), 0);

    CHECK_FALSE(failed);
    CHECK(edit.load_audio_call_count == 1);
    CHECK(edit.last_audio_asset == std::optional{failed_asset});
    CHECK(coordinator.session().arrangements().empty());
    CHECK(coordinator.session().currentArrangement() == nullptr);
    CHECK(coordinator.session().timeline() == core::TimeRange{});
}

// Verifies closing clears backend audio and restores the no-project session.
TEST_CASE("EditCoordinator closeSong clears audio and session", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    REQUIRE(coordinator.loadSong(makeSong(audio_asset.path), 0));

    coordinator.closeSong();

    CHECK(edit.clear_audio_call_count == 1);
    CHECK(coordinator.session().arrangements().empty());
    CHECK(coordinator.session().currentArrangement() == nullptr);
    CHECK(coordinator.session().timeline() == core::TimeRange{});
}

// Verifies a selected arrangement can be loaded without storing package IDs in core.
TEST_CASE("EditCoordinator loads the selected arrangement index", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    core::Song song;
    song.chart.arrangements.push_back(
        core::Arrangement{
            .part = core::Part::Lead,
            .difficulty = core::DifficultyRating{},
            .audio_asset = core::AudioAsset{std::filesystem::path{"lead.wav"}},
            .audio_duration = core::TimeDuration{},
            .tone_timeline_ref = {},
            .note_events = {},
        });
    song.chart.arrangements.push_back(
        core::Arrangement{
            .part = core::Part::Bass,
            .difficulty = core::DifficultyRating{},
            .audio_asset = core::AudioAsset{std::filesystem::path{"bass.wav"}},
            .audio_duration = core::TimeDuration{},
            .tone_timeline_ref = {},
            .note_events = {},
        });

    const bool loaded = coordinator.loadSong(std::move(song), 1);

    CHECK(loaded);
    CHECK(
        edit.last_audio_asset ==
        std::optional{core::AudioAsset{std::filesystem::path{"bass.wav"}}});
    REQUIRE(coordinator.session().currentArrangement() != nullptr);
    CHECK(coordinator.session().currentArrangement()->part == core::Part::Bass);
    CHECK(coordinator.session().currentArrangement()->audio_duration == core::TimeDuration{10.0});
}

} // namespace rock_hero::ui
