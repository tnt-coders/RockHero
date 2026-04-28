#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/chart.h>

namespace rock_hero::core
{

// Verifies charts can aggregate multiple part/difficulty variants for one song.
TEST_CASE("Chart holds multiple arrangements", "[core][chart]")
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
