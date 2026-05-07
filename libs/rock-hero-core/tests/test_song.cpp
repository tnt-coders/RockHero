#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/core/song.h>

namespace rock_hero::core
{

// Verifies that default song state is empty so loaders can detect unset metadata and assets.
TEST_CASE("Song default construction is empty", "[core][song]")
{
    const Song song;
    CHECK(song.metadata.title.empty());
    CHECK(song.metadata.artist.empty());
    CHECK(song.metadata.album.empty());
    CHECK(song.metadata.year == 0);
    CHECK(song.chart.arrangements.empty());
}

// Verifies metadata fields remain plain value storage until validation rules are introduced.
TEST_CASE("Song metadata round-trip", "[core][song]")
{
    Song song;
    song.metadata.title = "Test Song";
    song.metadata.artist = "Test Artist";
    song.metadata.album = "Test Album";
    song.metadata.year = 2026;
    CHECK(song.metadata.title == "Test Song");
    CHECK(song.metadata.artist == "Test Artist");
    CHECK(song.metadata.album == "Test Album");
    CHECK(song.metadata.year == 2026);
}

// Verifies Song stores all top-level value fields without imposing validation policy.
TEST_CASE("Song stores top-level value fields", "[core][song]")
{
    Song song;
    song.metadata.title = "Test Song";
    song.metadata.artist = "Test Artist";
    song.metadata.album = "Test Album";
    song.metadata.year = 2026;
    song.chart.arrangements.push_back(
        {.id = "rhythm",
         .part = Part::Rhythm,
         .difficulty = DifficultyRating{6},
         .audio_asset = AudioAsset{std::filesystem::path{"audio/rhythm.wav"}},
         .audio_duration = TimeDuration{42.0},
         .tone_timeline_ref = "tone/rhythm.json",
         .note_events = {
             {.position = TimePosition{3.0},
              .duration = TimeDuration{1.5},
              .string_number = 2,
              .fret = 7}
         }});

    CHECK(song.metadata.title == "Test Song");
    CHECK(song.metadata.artist == "Test Artist");
    CHECK(song.metadata.album == "Test Album");
    CHECK(song.metadata.year == 2026);
    REQUIRE(song.chart.arrangements.size() == 1);
    const Arrangement& arrangement = song.chart.arrangements[0];
    CHECK(arrangement.part == Part::Rhythm);
    CHECK(arrangement.difficulty == DifficultyRating{6});
    CHECK(difficultyTier(arrangement.difficulty) == DifficultyTier::Hard);
    REQUIRE(arrangement.audio_asset.has_value());
    if (arrangement.audio_asset.has_value())
    {
        CHECK(arrangement.audio_asset.value().path == std::filesystem::path{"audio/rhythm.wav"});
    }
    CHECK(arrangement.audio_duration == TimeDuration{42.0});
    CHECK(arrangement.tone_timeline_ref == "tone/rhythm.json");
    REQUIRE(arrangement.note_events.size() == 1);
    CHECK(arrangement.note_events[0].position == TimePosition{3.0});
    CHECK(arrangement.note_events[0].duration == TimeDuration{1.5});
    CHECK(arrangement.note_events[0].string_number == 2);
    CHECK(arrangement.note_events[0].fret == 7);
}

} // namespace rock_hero::core
