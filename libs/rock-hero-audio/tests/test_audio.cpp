#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/audio/i_audio.h>

namespace rock_hero::audio
{

namespace
{

// Builds a minimal one-arrangement song used to exercise the framework-free audio port.
[[nodiscard]] core::Song makeSong(std::filesystem::path audio_path)
{
    core::Song song;
    song.arrangements.push_back(
        core::Arrangement{
            .id = "lead",
            .part = core::Part::Lead,
            .difficulty = core::DifficultyRating{},
            .audio_asset = core::AudioAsset{std::move(audio_path)},
            .audio_duration = core::TimeDuration{},
            .tone_timeline_ref = {},
            .note_events = {},
        });
    return song;
}

// Test double that records song-preparation and active-arrangement requests.
class FakeAudio final : public IAudio
{
public:
    // Fills arrangement durations when the fake is configured to accept preparation.
    bool prepareSong(core::Song& song) override
    {
        ++prepare_song_call_count;
        if (!next_prepare_result)
        {
            return false;
        }

        for (core::Arrangement& arrangement : song.arrangements)
        {
            arrangement.audio_duration = prepared_duration;
        }
        return true;
    }

    // Records the prepared arrangement selected for backend playback.
    bool setActiveArrangement(const core::Arrangement& arrangement) override
    {
        last_active_audio_asset = arrangement.audio_asset;
        ++set_active_arrangement_call_count;
        return next_set_active_result;
    }

    // Records that the current backend arrangement should be cleared.
    void clearActiveArrangement() override
    {
        last_active_audio_asset.reset();
        ++clear_active_arrangement_call_count;
    }

    core::TimeDuration prepared_duration{core::TimeDuration{12.0}};
    bool next_prepare_result{true};
    bool next_set_active_result{true};
    std::optional<core::AudioAsset> last_active_audio_asset{};
    int prepare_song_call_count{0};
    int set_active_arrangement_call_count{0};
    int clear_active_arrangement_call_count{0};
};

} // namespace

// Verifies the audio port prepares candidate songs by filling arrangement durations.
TEST_CASE("IAudio prepares song audio", "[audio][audio]")
{
    FakeAudio audio;
    auto song = makeSong(std::filesystem::path{"drums.wav"});

    const bool prepared = audio.prepareSong(song);

    CHECK(prepared);
    REQUIRE(song.arrangements.size() == 1);
    CHECK(song.arrangements.front().audio_duration == core::TimeDuration{12.0});
    CHECK(audio.prepare_song_call_count == 1);
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Verifies audio adapters can reject song preparation without activating an arrangement.
TEST_CASE("IAudio song preparation can fail", "[audio][audio]")
{
    FakeAudio audio;
    audio.next_prepare_result = false;
    auto song = makeSong(std::filesystem::path{"missing.wav"});

    const bool prepared = audio.prepareSong(song);

    CHECK_FALSE(prepared);
    CHECK(audio.prepare_song_call_count == 1);
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Verifies the audio port exposes active-arrangement replacement semantics.
TEST_CASE("IAudio sets active arrangement", "[audio][audio]")
{
    FakeAudio audio;
    auto song = makeSong(std::filesystem::path{"drums.wav"});
    REQUIRE(audio.prepareSong(song));
    REQUIRE(song.arrangements.size() == 1);

    const bool active_set = audio.setActiveArrangement(song.arrangements.front());

    CHECK(active_set);
    CHECK(
        audio.last_active_audio_asset ==
        std::optional{core::AudioAsset{std::filesystem::path{"drums.wav"}}});
    CHECK(audio.set_active_arrangement_call_count == 1);
}

// Verifies the audio port exposes a command to clear the current active arrangement.
TEST_CASE("IAudio clears active arrangement", "[audio][audio]")
{
    FakeAudio audio;
    auto song = makeSong(std::filesystem::path{"drums.wav"});
    REQUIRE(audio.prepareSong(song));
    REQUIRE(song.arrangements.size() == 1);
    REQUIRE(audio.setActiveArrangement(song.arrangements.front()));

    audio.clearActiveArrangement();

    CHECK_FALSE(audio.last_active_audio_asset.has_value());
    CHECK(audio.clear_active_arrangement_call_count == 1);
}

} // namespace rock_hero::audio
