#include <catch2/catch_test_macros.hpp>

#include <song/arrangement.h>
#include <song/chart.h>
#include <song/song.h>

TEST_CASE("Song default construction is empty", "[song]")
{
    const rock_hero::Song song;
    REQUIRE(song.metadata.title.empty());
    REQUIRE(song.metadata.artist.empty());
    REQUIRE(song.audio_asset_ref.empty());
    REQUIRE(song.tone_timeline_ref.empty());
    REQUIRE(song.chart.arrangements.empty());
}

TEST_CASE("Song metadata round-trip", "[song]")
{
    rock_hero::Song song;
    song.metadata.title = "Test Song";
    song.metadata.artist = "Test Artist";
    song.metadata.year = 2026;
    REQUIRE(song.metadata.title == "Test Song");
    REQUIRE(song.metadata.year == 2026);
}

TEST_CASE("Arrangement holds note events", "[arrangement]")
{
    rock_hero::Arrangement arr;
    arr.part = rock_hero::Part::Lead;
    arr.difficulty = rock_hero::Difficulty::Expert;
    arr.note_events.push_back(
        {.time_seconds = 1.0, .duration_seconds = 0.5, .string_number = 1, .fret = 5});
    arr.note_events.push_back(
        {.time_seconds = 2.0, .duration_seconds = 0.0, .string_number = 6, .fret = 0});

    REQUIRE(arr.note_events.size() == 2);
    REQUIRE(arr.note_events[0].time_seconds == 1.0);
    REQUIRE(arr.note_events[0].duration_seconds == 0.5);
    REQUIRE(arr.note_events[1].fret == 0);
}

TEST_CASE("Chart holds multiple arrangements", "[chart]")
{
    rock_hero::Chart chart;
    chart.arrangements.push_back(
        {.part = rock_hero::Part::Lead,
         .difficulty = rock_hero::Difficulty::Easy,
         .note_events = {}});
    chart.arrangements.push_back(
        {.part = rock_hero::Part::Bass,
         .difficulty = rock_hero::Difficulty::Expert,
         .note_events = {}});

    REQUIRE(chart.arrangements.size() == 2);
    REQUIRE(chart.arrangements[0].part == rock_hero::Part::Lead);
    REQUIRE(chart.arrangements[1].difficulty == rock_hero::Difficulty::Expert);
}
