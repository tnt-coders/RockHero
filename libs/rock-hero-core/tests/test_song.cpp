#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/arrangement.h>
#include <rock_hero/core/chart.h>
#include <rock_hero/core/song.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::core
{

// Verifies that default song state is empty so loaders can detect unset metadata and assets.
TEST_CASE("Song default construction is empty", "[song]")
{
    const Song song;
    CHECK(song.metadata.title.empty());
    CHECK(song.metadata.artist.empty());
    CHECK(song.audio_asset_ref.empty());
    CHECK(song.tone_timeline_ref.empty());
    CHECK(song.chart.arrangements.empty());
}

// Verifies metadata fields remain plain value storage until validation rules are introduced.
TEST_CASE("Song metadata round-trip", "[song]")
{
    Song song;
    song.metadata.title = "Test Song";
    song.metadata.artist = "Test Artist";
    song.metadata.year = 2026;
    CHECK(song.metadata.title == "Test Song");
    CHECK(song.metadata.year == 2026);
}

// Verifies arrangements own note-event sequences with timing, string, and fret data intact.
TEST_CASE("Arrangement holds note events", "[arrangement]")
{
    Arrangement arr;
    arr.part = Part::Lead;
    arr.difficulty = Difficulty::Expert;
    arr.note_events.push_back(
        {.position = TimePosition{1.0},
         .duration = TimeDuration{0.5},
         .string_number = 1,
         .fret = 5});
    arr.note_events.push_back(
        {.position = TimePosition{2.0}, .duration = TimeDuration{}, .string_number = 6, .fret = 0});

    REQUIRE(arr.note_events.size() == 2);
    CHECK(arr.note_events[0].position.seconds == 1.0);
    CHECK(arr.note_events[0].duration.seconds == 0.5);
    CHECK(arr.note_events[1].fret == 0);
}

// Verifies timeline value types preserve their underlying second values.
TEST_CASE("Time value wrappers store seconds", "[arrangement]")
{
    const TimePosition position{12.5};
    const TimeDuration duration{48.0};

    CHECK(position.seconds == 12.5);
    CHECK(duration.seconds == 48.0);
}

// Verifies charts can aggregate multiple part/difficulty variants for one song.
TEST_CASE("Chart holds multiple arrangements", "[chart]")
{
    Chart chart;
    chart.arrangements.push_back(
        {.part = Part::Lead, .difficulty = Difficulty::Easy, .note_events = {}});
    chart.arrangements.push_back(
        {.part = Part::Bass, .difficulty = Difficulty::Expert, .note_events = {}});

    REQUIRE(chart.arrangements.size() == 2);
    CHECK(chart.arrangements[0].part == Part::Lead);
    CHECK(chart.arrangements[1].difficulty == Difficulty::Expert);
}

} // namespace rock_hero::core
