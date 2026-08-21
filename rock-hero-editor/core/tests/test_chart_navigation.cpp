#include "chart/chart_navigation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <vector>

namespace rock_hero::editor::core
{

// The pure half of the caret's navigation, reachable from a test now that it is out of the
// controller: the destinations every caret verb and the time-selection extend resolve through, the
// fret-entry window's extendable test, and the caret's published time bounds.
TEST_CASE("Chart navigation resolves caret destinations", "[core][chart]")
{
    SECTION("the fret entry waits only for a value another digit can still extend")
    {
        // {1, 2} can become 10-24 under the 24-fret cap; 3 cannot, and 0 never waits.
        CHECK(chartFretValueExtendable(1));
        CHECK(chartFretValueExtendable(2));
        CHECK_FALSE(chartFretValueExtendable(3));
        CHECK_FALSE(chartFretValueExtendable(0));
        CHECK_FALSE(chartFretValueExtendable(common::core::g_max_fret));
    }

    SECTION("the measure jump lands on downbeats the Guitar Pro way")
    {
        const common::core::GridPosition mid{.measure = 3, .beat = 2, .offset = {}};
        CHECK(
            measureJumpPosition(mid, true) ==
            common::core::GridPosition{.measure = 4, .beat = 1, .offset = {}});
        // Earlier from mid-measure is the current downbeat; from a downbeat, the previous one.
        CHECK(
            measureJumpPosition(mid, false) ==
            common::core::GridPosition{.measure = 3, .beat = 1, .offset = {}});
        const common::core::GridPosition downbeat{.measure = 3, .beat = 1, .offset = {}};
        CHECK(
            measureJumpPosition(downbeat, false) ==
            common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}});
        // The first measure has no predecessor.
        CHECK(measureJumpPosition(chartStartPosition(), false) == chartStartPosition());
    }

    SECTION("the adjacent section is the nearest strictly past the reference")
    {
        const std::vector<common::core::SongSection> sections{
            common::core::SongSection{
                .position = {.measure = 1, .beat = 1, .offset = {}}, .name = "Intro"
            },
            common::core::SongSection{
                .position = {.measure = 5, .beat = 1, .offset = {}}, .name = "Verse"
            },
            common::core::SongSection{
                .position = {.measure = 9, .beat = 1, .offset = {}}, .name = "Chorus"
            },
        };
        const common::core::GridPosition inside_verse{.measure = 6, .beat = 1, .offset = {}};
        CHECK(
            adjacentSectionPosition(sections, inside_verse, true) ==
            std::optional{common::core::GridPosition{.measure = 9, .beat = 1, .offset = {}}});
        CHECK(
            adjacentSectionPosition(sections, inside_verse, false) ==
            std::optional{common::core::GridPosition{.measure = 5, .beat = 1, .offset = {}}});
        // Strictly: standing ON a section start, "earlier" is the one before it.
        const common::core::GridPosition on_verse{.measure = 5, .beat = 1, .offset = {}};
        CHECK(
            adjacentSectionPosition(sections, on_verse, false) ==
            std::optional{common::core::GridPosition{.measure = 1, .beat = 1, .offset = {}}});
        // Nothing past the last one, nothing before the first.
        const common::core::GridPosition past{.measure = 12, .beat = 1, .offset = {}};
        CHECK_FALSE(adjacentSectionPosition(sections, past, true).has_value());
        CHECK_FALSE(adjacentSectionPosition(sections, chartStartPosition(), false).has_value());
    }

    SECTION("the caret's time bounds are its seconds and its measure's span")
    {
        // 120 BPM 4/4: a measure is two seconds.
        const common::core::TempoMap tempo_map =
            common::core::TempoMap::defaultMap(common::core::TimeDuration{30.0});
        const CaretTimeBounds bounds =
            caretTimeBounds(tempo_map, {.measure = 2, .beat = 3, .offset = {}});
        CHECK(bounds.seconds == Catch::Approx(3.0));
        CHECK(bounds.measure_start_seconds == Catch::Approx(2.0));
        CHECK(bounds.measure_end_seconds == Catch::Approx(4.0));
    }
}

} // namespace rock_hero::editor::core
