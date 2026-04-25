#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/arrangement.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::core
{

// Verifies arrangements own note-event sequences with timing, string, and fret data intact.
TEST_CASE("Arrangement holds note events", "[core][arrangement]")
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
TEST_CASE("Time value wrappers store seconds", "[core][arrangement]")
{
    const TimePosition position{12.5};
    const TimeDuration duration{48.0};

    CHECK(position.seconds == 12.5);
    CHECK(duration.seconds == 48.0);
}

} // namespace rock_hero::core
