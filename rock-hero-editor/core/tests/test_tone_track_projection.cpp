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

// Builds a 4/4 120 BPM map whose measure 1 downbeat sits at 1.0 s, so the chart has a one-second
// lead-in before the first beat (measure 2 lands at 3.0 s, measure 3 at 5.0 s).
[[nodiscard]] common::core::TempoMap makeLeadInTempoMap()
{
    return common::core::TempoMap{
        {common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 1.0},
            common::core::BeatAnchor{.measure = 3, .beat = 1, .seconds = 5.0},
        },
    };
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
    arrangement.tones = {
        common::core::Tone{
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
            .name = "Clean Verse",
        },
    };
    arrangement.tone_track.regions = {
        common::core::ToneRegion{
            .id = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e",
            .start = common::core::GridPosition{.measure = 1, .beat = 1},
            .end = common::core::GridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
        },
    };

    const ToneTrackViewState state =
        makeToneTrackViewState(arrangement, makeTempoMap(), std::string{}, std::string{});

    REQUIRE(state.regions.size() == 1);
    CHECK(state.regions.front().id == arrangement.tone_track.regions.front().id);
    CHECK(state.regions.front().name == "Clean Verse");
    CHECK(state.regions.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(state.regions.front().time_range.end.seconds == Catch::Approx(2.0));
    CHECK(state.regions.front().grid_start == common::core::GridPosition{.measure = 1, .beat = 1});
    CHECK(state.regions.front().grid_end == common::core::GridPosition{.measure = 2, .beat = 1});
    CHECK_FALSE(state.regions.front().active);
    CHECK_FALSE(state.regions.front().selected);
}

TEST_CASE(
    "Tone track projection extends the baseline region back to the timeline origin", "[core][tone]")
{
    common::core::Arrangement arrangement = makeArrangement();
    arrangement.tones = {
        common::core::Tone{
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
            .name = "Lead-in Clean",
        },
        common::core::Tone{
            .tone_document_ref = "tones/1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d/tone.json",
            .name = "Drive",
        },
    };
    arrangement.tone_track.regions = {
        common::core::ToneRegion{
            .id = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e",
            .start = common::core::GridPosition{.measure = 1, .beat = 1},
            .end = common::core::GridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
        },
        common::core::ToneRegion{
            .id = "6b2f1d4e-8f3c-4b0d-9e2f-3a4b5c6d7e8f",
            .start = common::core::GridPosition{.measure = 2, .beat = 1},
            .end = common::core::GridPosition{.measure = 3, .beat = 1},
            .tone_document_ref = "tones/1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d/tone.json",
        },
    };

    const ToneTrackViewState state =
        makeToneTrackViewState(arrangement, makeLeadInTempoMap(), std::string{}, std::string{});

    REQUIRE(state.regions.size() == 2);
    // The baseline's grid start is measure 1 beat 1 (1.0 s), but it owns the lead-in, so its
    // displayed and selectable span reaches the timeline origin rather than starting at 1.0 s.
    CHECK(state.regions.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(state.regions.front().time_range.end.seconds == Catch::Approx(3.0));
    CHECK(state.regions.front().grid_start == common::core::GridPosition{.measure = 1, .beat = 1});
    // A later region keeps its authored grid start; only the baseline reaches back to the origin.
    CHECK(state.regions.back().time_range.start.seconds == Catch::Approx(3.0));
    CHECK(state.regions.back().time_range.end.seconds == Catch::Approx(5.0));
}

TEST_CASE(
    "Tone track projection marks the active and selected regions independently", "[core][tone]")
{
    common::core::Arrangement arrangement = makeArrangement();
    arrangement.tone_track.regions = {
        common::core::ToneRegion{
            .id = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e",
            .start = common::core::GridPosition{.measure = 1, .beat = 1},
            .end = common::core::GridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
        },
    };
    const std::string region_id = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e";

    SECTION("active without a formal selection sets only the active flag")
    {
        const ToneTrackViewState state =
            makeToneTrackViewState(arrangement, makeTempoMap(), region_id, std::string{});

        REQUIRE(state.regions.size() == 1);
        CHECK(state.regions.front().active);
        CHECK_FALSE(state.regions.front().selected);
    }

    SECTION("a formal selection sets both flags when the region is also active")
    {
        const ToneTrackViewState state =
            makeToneTrackViewState(arrangement, makeTempoMap(), region_id, region_id);

        REQUIRE(state.regions.size() == 1);
        CHECK(state.regions.front().active);
        CHECK(state.regions.front().selected);
    }
}

TEST_CASE("Tone track projection renders nothing without explicit regions", "[core][tone]")
{
    // The load baseline guarantees explicit regions, so a region-less arrangement (only possible
    // outside a loaded session) draws no synthetic content even with a cataloged tone.
    common::core::Arrangement arrangement = makeArrangement();
    arrangement.tones = {
        common::core::Tone{
            .tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
            .name = "Default",
        },
    };

    const ToneTrackViewState state =
        makeToneTrackViewState(arrangement, makeTempoMap(), std::string{}, std::string{});

    CHECK(state.regions.empty());
}

TEST_CASE("Tone track projection is empty without tones", "[core][tone]")
{
    const ToneTrackViewState state =
        makeToneTrackViewState(makeArrangement(), makeTempoMap(), std::string{}, std::string{});

    CHECK(state.regions.empty());
}

} // namespace rock_hero::editor::core
