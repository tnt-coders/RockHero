#include "timeline/section_projection.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::GridPosition;
using common::core::SongSection;

// A 4/4 default map: measure 1 beat 1 sits at zero and beats last half a second at 120 BPM.
[[nodiscard]] common::core::TempoMap makeTempoMap()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
}

} // namespace

TEST_CASE("Section projection resolves song sections to seconds", "[editor-core][sections]")
{
    const std::vector<SongSection> sections{
        SongSection{.position = GridPosition{.measure = 1, .beat = 1}, .name = "intro"},
        SongSection{.position = GridPosition{.measure = 3, .beat = 1}, .name = "verse"},
    };

    const std::vector<SongSectionView> views = makeSongSectionViews(sections, makeTempoMap());

    // Measure 1 beat 1 sits at zero; measure 3 beat 1 is beat index 8, half a second per beat.
    REQUIRE(views.size() == 2);
    CHECK(views[0].seconds == Catch::Approx(0.0));
    CHECK(views[0].name == "intro");
    CHECK(views[1].seconds == Catch::Approx(8.0 * 0.5));
    CHECK(views[1].name == "verse");
}

TEST_CASE("Section projection of an empty song is empty", "[editor-core][sections]")
{
    CHECK(makeSongSectionViews({}, makeTempoMap()).empty());
}

} // namespace rock_hero::editor::core
