#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/chart.h>

namespace rock_hero::core
{

// Verifies a standalone chart has no arrangements until authoring adds them.
TEST_CASE("Chart default construction is empty", "[core][chart]")
{
    const Chart chart;

    CHECK(chart.arrangements.empty());
}

// Verifies charts can aggregate multiple part/difficulty-rating variants for one song.
TEST_CASE("Chart holds multiple arrangements", "[core][chart]")
{
    Chart chart;
    chart.arrangements.push_back(
        {.part = Part::Lead, .difficulty = DifficultyRating{2}, .note_events = {}});
    chart.arrangements.push_back(
        {.part = Part::Bass, .difficulty = DifficultyRating{9}, .note_events = {}});

    REQUIRE(chart.arrangements.size() == 2);
    CHECK(chart.arrangements[0].part == Part::Lead);
    CHECK(chart.arrangements[0].difficulty == DifficultyRating{2});
    CHECK(difficultyTier(chart.arrangements[0].difficulty) == DifficultyTier::Easy);
    CHECK(chart.arrangements[0].note_events.empty());
    CHECK(chart.arrangements[1].part == Part::Bass);
    CHECK(chart.arrangements[1].difficulty == DifficultyRating{9});
    CHECK(difficultyTier(chart.arrangements[1].difficulty) == DifficultyTier::Master);
    CHECK(chart.arrangements[1].note_events.empty());
}

} // namespace rock_hero::core
