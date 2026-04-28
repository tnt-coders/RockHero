#include <catch2/catch_test_macros.hpp>
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
    CHECK(song.audio_asset_ref.empty());
    CHECK(song.tone_timeline_ref.empty());
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
    song.audio_asset_ref = "audio/backing.wav";
    song.tone_timeline_ref = "tone/timeline.json";
    song.chart.arrangements.push_back(
        {.part = Part::Rhythm,
         .difficulty = DifficultyRating{6},
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
    CHECK(song.audio_asset_ref == "audio/backing.wav");
    CHECK(song.tone_timeline_ref == "tone/timeline.json");
    REQUIRE(song.chart.arrangements.size() == 1);
    CHECK(song.chart.arrangements[0].part == Part::Rhythm);
    CHECK(song.chart.arrangements[0].difficulty == DifficultyRating{6});
    CHECK(difficultyTier(song.chart.arrangements[0].difficulty) == DifficultyTier::Hard);
    REQUIRE(song.chart.arrangements[0].note_events.size() == 1);
    CHECK(song.chart.arrangements[0].note_events[0].position == TimePosition{3.0});
    CHECK(song.chart.arrangements[0].note_events[0].duration == TimeDuration{1.5});
    CHECK(song.chart.arrangements[0].note_events[0].string_number == 2);
    CHECK(song.chart.arrangements[0].note_events[0].fret == 7);
}

} // namespace rock_hero::core
