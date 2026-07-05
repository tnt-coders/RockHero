#include "tone/tone_track_projection.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <string>

namespace rock_hero::editor::core
{

namespace
{

// Builds a 4/4 map at 120 BPM whose terminal downbeat sits at measure 3 beat 1 (4.0 seconds).
[[nodiscard]] common::core::TempoMap makeTempoMap()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
}

// Builds a minimal arrangement carrying only the tone fields the projection reads.
[[nodiscard]] common::core::Arrangement makeArrangement()
{
    common::core::Arrangement arrangement;
    arrangement.id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4";
    return arrangement;
}

} // namespace

TEST_CASE("Tone track projection resolves authored regions to seconds", "[core][tone]")
{
    common::core::Arrangement arrangement = makeArrangement();
    arrangement.tone_track.regions = {
        common::core::ToneRegion{
            .id = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e",
            .name = "Clean Verse",
            .start = common::core::ToneGridPosition{.measure = 1, .beat = 1},
            .end = common::core::ToneGridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
        },
    };

    const ToneTrackViewState state =
        toneTrackViewStateFor(arrangement, makeTempoMap(), std::string{});

    REQUIRE(state.regions.size() == 1);
    CHECK(state.regions.front().id == arrangement.tone_track.regions.front().id);
    CHECK(state.regions.front().name == "Clean Verse");
    CHECK_FALSE(state.regions.front().synthesized_default);
    CHECK(state.regions.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(state.regions.front().time_range.end.seconds == Catch::Approx(2.0));
    CHECK(
        state.regions.front().grid_start ==
        common::core::ToneGridPosition{.measure = 1, .beat = 1});
    CHECK(
        state.regions.front().grid_end == common::core::ToneGridPosition{.measure = 2, .beat = 1});
    CHECK_FALSE(state.regions.front().selected);
}

TEST_CASE("Tone track projection marks the selected region", "[core][tone]")
{
    common::core::Arrangement arrangement = makeArrangement();
    arrangement.tone_track.regions = {
        common::core::ToneRegion{
            .id = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e",
            .name = "Clean Verse",
            .start = common::core::ToneGridPosition{.measure = 1, .beat = 1},
            .end = common::core::ToneGridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
        },
    };

    const ToneTrackViewState state =
        toneTrackViewStateFor(arrangement, makeTempoMap(), "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e");

    REQUIRE(state.regions.size() == 1);
    CHECK(state.regions.front().selected);
}

TEST_CASE("Tone track projection synthesizes a legacy default region", "[core][tone]")
{
    common::core::Arrangement arrangement = makeArrangement();
    arrangement.tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json";

    const ToneTrackViewState state =
        toneTrackViewStateFor(arrangement, makeTempoMap(), std::string{});

    REQUIRE(state.regions.size() == 1);
    CHECK(state.regions.front().id.empty());
    CHECK(state.regions.front().name.empty());
    CHECK(state.regions.front().synthesized_default);
    CHECK(state.regions.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(state.regions.front().time_range.end.seconds == Catch::Approx(4.0));
}

TEST_CASE("Tone track projection is empty without tones", "[core][tone]")
{
    const ToneTrackViewState state =
        toneTrackViewStateFor(makeArrangement(), makeTempoMap(), std::string{});

    CHECK(state.regions.empty());
}

} // namespace rock_hero::editor::core
