#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/testing/configurable_song_audio.h>
#include <string>

namespace rock_hero::common::audio
{

namespace
{

// Builds a minimal one-arrangement song used to exercise the framework-free audio port.
[[nodiscard]] common::core::Song makeSong(std::filesystem::path audio_path)
{
    common::core::Song song;
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = "lead",
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = common::core::AudioAsset{std::move(audio_path)},
            .audio_duration = common::core::TimeDuration{},
            .tone_document_ref = {},
            .note_events = {},
        });
    return song;
}

} // namespace

// Verifies the audio port prepares candidate songs by filling arrangement durations.
TEST_CASE("ISongAudio prepares song audio", "[audio][song-audio]")
{
    testing::ConfigurableSongAudio audio;
    audio.next_prepared_audio_duration = common::core::TimeDuration{12.0};
    auto song = makeSong(std::filesystem::path{"drums.wav"});

    const auto prepared = audio.prepareSong(song);

    CHECK(prepared.has_value());
    REQUIRE(song.arrangements.size() == 1);
    CHECK(song.arrangements.front().audio_duration == common::core::TimeDuration{12.0});
    CHECK(audio.prepare_song_call_count == 1);
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Verifies audio adapters report typed preparation failures with useful context.
TEST_CASE("ISongAudio reports typed preparation failure", "[audio][song-audio]")
{
    testing::ConfigurableSongAudio audio;
    audio.next_prepare_error = SongAudioError{
        SongAudioErrorCode::UnreadableAudioFile,
        "Configured song preparation failure for: missing.wav",
    };
    auto song = makeSong(std::filesystem::path{"missing.wav"});

    const auto prepared = audio.prepareSong(song);

    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == SongAudioErrorCode::UnreadableAudioFile);
    CHECK(prepared.error().message.find("missing.wav") != std::string::npos);
    CHECK(audio.prepare_song_call_count == 1);
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Verifies the audio port exposes active-arrangement replacement semantics.
TEST_CASE("ISongAudio sets active arrangement", "[audio][song-audio]")
{
    testing::ConfigurableSongAudio audio;
    auto song = makeSong(std::filesystem::path{"drums.wav"});
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);

    const auto active_set = audio.setActiveArrangement(song.arrangements.front());

    CHECK(active_set.has_value());
    CHECK(
        audio.last_active_audio_asset ==
        std::optional{common::core::AudioAsset{std::filesystem::path{"drums.wav"}}});
    CHECK(audio.set_active_arrangement_call_count == 1);
}

// Verifies active-arrangement setup failures keep their stable boundary code.
TEST_CASE("ISongAudio reports typed activation failure", "[audio][song-audio]")
{
    testing::ConfigurableSongAudio audio;
    audio.next_set_active_arrangement_error = SongAudioError{
        SongAudioErrorCode::MonitoringRouteFailed,
        "Monitoring route rebuild failed",
    };
    auto song = makeSong(std::filesystem::path{"drums.wav"});
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);

    const auto active_set = audio.setActiveArrangement(song.arrangements.front());

    REQUIRE_FALSE(active_set.has_value());
    CHECK(active_set.error().code == SongAudioErrorCode::MonitoringRouteFailed);
    CHECK(active_set.error().message == "Monitoring route rebuild failed");
    CHECK(audio.set_active_arrangement_call_count == 1);
}

// Verifies the audio port exposes a command to clear the current active arrangement.
TEST_CASE("ISongAudio clears active arrangement", "[audio][song-audio]")
{
    testing::ConfigurableSongAudio audio;
    auto song = makeSong(std::filesystem::path{"drums.wav"});
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);
    REQUIRE(audio.setActiveArrangement(song.arrangements.front()).has_value());

    REQUIRE(audio.clearActiveArrangement().has_value());

    CHECK_FALSE(audio.last_active_audio_asset.has_value());
    CHECK(audio.clear_active_arrangement_call_count == 1);
}

// Verifies active-arrangement cleanup failures remain branchable for callers.
TEST_CASE("ISongAudio reports typed clear failure", "[audio][song-audio]")
{
    testing::ConfigurableSongAudio audio;
    audio.next_clear_active_arrangement_error = SongAudioError{
        SongAudioErrorCode::MonitoringRouteFailed,
        "Monitoring route cleanup failed",
    };

    const auto cleared = audio.clearActiveArrangement();

    REQUIRE_FALSE(cleared.has_value());
    CHECK(cleared.error().code == SongAudioErrorCode::MonitoringRouteFailed);
    CHECK(cleared.error().message == "Monitoring route cleanup failed");
    CHECK(audio.clear_active_arrangement_call_count == 1);
}

} // namespace rock_hero::common::audio
