#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/ui/edit_coordinator.h>

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

    std::optional<core::TimeDuration> next_load_audio_result{core::TimeDuration{10.0}};
    int load_audio_call_count{0};
    std::optional<core::AudioAsset> last_audio_asset{};
};

} // namespace

// Verifies the coordinator exposes a session with the temporary empty arrangement shell.
TEST_CASE("EditCoordinator owns an arrangement session", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    const EditCoordinator coordinator{edit};

    REQUIRE(coordinator.session().arrangements().size() == 1);
    CHECK(
        coordinator.session().currentArrangement() ==
        &coordinator.session().arrangements().front());
    CHECK_FALSE(coordinator.session().arrangements().front().hasAudio());
    CHECK(edit.load_audio_call_count == 0);
}

// Verifies the coordinator asks the backend and commits accepted arrangement audio.
TEST_CASE("EditCoordinator stores accepted arrangement audio", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    const bool audio_set = coordinator.setArrangementAudio(audio_asset);

    CHECK(audio_set);
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

// Verifies failed backend audio loading leaves the session arrangement unchanged.
TEST_CASE(
    "EditCoordinator preserves session on arrangement-audio failure", "[ui][edit-coordinator]")
{
    FakeEdit edit;
    EditCoordinator coordinator{edit};
    edit.next_load_audio_result = std::nullopt;
    const core::AudioAsset failed_asset{std::filesystem::path{"missing.wav"}};

    const bool failed = coordinator.setArrangementAudio(failed_asset);

    CHECK_FALSE(failed);
    CHECK(edit.load_audio_call_count == 1);
    CHECK(edit.last_audio_asset == std::optional{failed_asset});
    REQUIRE(coordinator.session().arrangements().size() == 1);
    CHECK_FALSE(coordinator.session().arrangements().front().hasAudio());
    CHECK(coordinator.session().timeline() == core::TimeRange{});
}

} // namespace rock_hero::ui
