#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/arrangement.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::core
{

// Verifies an unset note event starts at zero time with no fret/string assignment.
TEST_CASE("NoteEvent default construction is empty", "[core][arrangement]")
{
    const NoteEvent note;

    CHECK(note.position == TimePosition{});
    CHECK(note.duration == TimeDuration{});
    CHECK(note.string_number == 0);
    CHECK(note.fret == 0);
}

// Verifies Arrangement's default part, unknown difficulty, and note storage contract.
TEST_CASE("Arrangement default construction is lead unrated", "[core][arrangement]")
{
    const Arrangement arrangement;

    CHECK(arrangement.part == Part::Lead);
    CHECK(arrangement.difficulty == DifficultyRating{});
    CHECK(difficultyTier(arrangement.difficulty) == DifficultyTier::Unknown);
    CHECK(arrangement.note_events.empty());
}

// Verifies arrangements own note-event sequences with timing, string, and fret data intact.
TEST_CASE("Arrangement holds note events", "[core][arrangement]")
{
    Arrangement arr;
    arr.part = Part::Lead;
    arr.difficulty = DifficultyRating{8};
    arr.note_events.push_back(
        {.position = TimePosition{1.0},
         .duration = TimeDuration{0.5},
         .string_number = 1,
         .fret = 5});
    arr.note_events.push_back(
        {.position = TimePosition{2.0}, .duration = TimeDuration{}, .string_number = 6, .fret = 0});

    CHECK(arr.part == Part::Lead);
    CHECK(arr.difficulty == DifficultyRating{8});
    CHECK(difficultyTier(arr.difficulty) == DifficultyTier::Expert);
    REQUIRE(arr.note_events.size() == 2);
    CHECK(arr.note_events[0].position.seconds == 1.0);
    CHECK(arr.note_events[0].duration.seconds == 0.5);
    CHECK(arr.note_events[0].string_number == 1);
    CHECK(arr.note_events[0].fret == 5);
    CHECK(arr.note_events[1].position.seconds == 2.0);
    CHECK(arr.note_events[1].duration.seconds == 0.0);
    CHECK(arr.note_events[1].string_number == 6);
    CHECK(arr.note_events[1].fret == 0);
}

// Verifies timeline value wrappers default to the start of the song.
TEST_CASE("Time value wrappers default to zero seconds", "[core][timeline]")
{
    const TimePosition position;
    const TimeDuration duration;

    CHECK(position.seconds == 0.0);
    CHECK(duration.seconds == 0.0);
}

// Verifies timeline value types preserve their underlying second values.
TEST_CASE("Time value wrappers store seconds", "[core][timeline]")
{
    const TimePosition position{12.5};
    const TimeDuration duration{48.0};

    CHECK(position.seconds == 12.5);
    CHECK(duration.seconds == 48.0);
}

// Verifies timeline equality compares the wrapped second value.
TEST_CASE("Time value wrappers compare stored seconds", "[core][timeline]")
{
    CHECK(TimePosition{12.5} == TimePosition{12.5});
    CHECK_FALSE(TimePosition{12.5} == TimePosition{12.0});
    CHECK(TimeDuration{48.0} == TimeDuration{48.0});
    CHECK_FALSE(TimeDuration{48.0} == TimeDuration{49.0});
}

} // namespace rock_hero::core
