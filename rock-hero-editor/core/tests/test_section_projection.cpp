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

TEST_CASE("Section projection resolves song sections to seconds", "[core][sections]")
{
    const std::vector<SongSection> sections{
        SongSection{.position = GridPosition{.measure = 1, .beat = 1}, .name = "intro"},
        SongSection{.position = GridPosition{.measure = 3, .beat = 1}, .name = "verse"},
    };

    const std::vector<SongSectionViewState> views = makeSongSectionViews(sections, makeTempoMap());

    // Measure 1 beat 1 sits at zero; measure 3 beat 1 is beat index 8, half a second per beat.
    REQUIRE(views.size() == 2);
    CHECK(views[0].seconds == Catch::Approx(0.0));
    CHECK(views[0].name == "intro");
    CHECK(views[1].seconds == Catch::Approx(8.0 * 0.5));
    CHECK(views[1].name == "verse");

    // The stored position rides along beside the seconds so hit-testing and the authoring verbs
    // address a section by what the song stores rather than by inverting the pixel mapping.
    CHECK(views[0].position == sections[0].position);
    CHECK(views[1].position == sections[1].position);
    // No selection was passed, so nothing is outlined.
    CHECK_FALSE(views[0].selected);
    CHECK_FALSE(views[1].selected);
}

// The selection flag rides the view rather than being published beside it, exactly as a tone
// region's does, and only the section at the selected position carries it.
TEST_CASE("Section projection marks the selected section", "[core][sections]")
{
    const std::vector<SongSection> sections{
        SongSection{.position = GridPosition{.measure = 1, .beat = 1}, .name = "intro"},
        SongSection{.position = GridPosition{.measure = 3, .beat = 1}, .name = "verse"},
    };

    const std::vector<SongSectionViewState> views =
        makeSongSectionViews(sections, makeTempoMap(), GridPosition{.measure = 3, .beat = 1});

    REQUIRE(views.size() == 2);
    CHECK_FALSE(views[0].selected);
    CHECK(views[1].selected);

    // A position no section starts at selects nothing rather than the nearest one.
    const std::vector<SongSectionViewState> stale =
        makeSongSectionViews(sections, makeTempoMap(), GridPosition{.measure = 2, .beat = 1});
    REQUIRE(stale.size() == 2);
    CHECK_FALSE(stale[0].selected);
    CHECK_FALSE(stale[1].selected);
}

TEST_CASE("Section projection of an empty song is empty", "[core][sections]")
{
    CHECK(makeSongSectionViews({}, makeTempoMap()).empty());
}

} // namespace rock_hero::editor::core
