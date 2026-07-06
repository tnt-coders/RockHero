#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <string>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Builds a region for edit tests. Endpoints are not validated here; the edit functions operate
// purely on the region vector and leave grid/id validation to validateToneTrackRules.
[[nodiscard]] ToneRegion makeRegion(
    std::string id, ToneGridPosition start, ToneGridPosition end,
    std::string tone_ref = "tones/x/tone.json")
{
    return ToneRegion{
        .id = std::move(id),
        .name = "Lead",
        .start = start,
        .end = end,
        .tone_document_ref = std::move(tone_ref),
    };
}

} // namespace

TEST_CASE("sliceToneRegion splits a region into two adjacent halves", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion(
        "a", ToneGridPosition{.measure = 1, .beat = 1}, ToneGridPosition{.measure = 5, .beat = 1}));

    const auto result = sliceToneRegion(
        track, "a", ToneGridPosition{.measure = 3, .beat = 1}, "b", "tones/y/tone.json");

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].id == "a");
    CHECK(track.regions[0].start == ToneGridPosition{.measure = 1, .beat = 1});
    CHECK(track.regions[0].end == ToneGridPosition{.measure = 3, .beat = 1});
    CHECK(track.regions[1].id == "b");
    CHECK(track.regions[1].name == "Lead"); // name copied from the source region
    CHECK(track.regions[1].start == ToneGridPosition{.measure = 3, .beat = 1});
    CHECK(track.regions[1].end == ToneGridPosition{.measure = 5, .beat = 1});
    CHECK(track.regions[1].tone_document_ref == "tones/y/tone.json");
}

TEST_CASE("sliceToneRegion rejects a cut outside the region", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion(
        "a", ToneGridPosition{.measure = 1, .beat = 1}, ToneGridPosition{.measure = 3, .beat = 1}));

    // The start, the end, and past the end are all not strictly inside the region.
    for (const ToneGridPosition cut :
         {ToneGridPosition{.measure = 1, .beat = 1},
          ToneGridPosition{.measure = 3, .beat = 1},
          ToneGridPosition{.measure = 5, .beat = 1}})
    {
        const auto result = sliceToneRegion(track, "a", cut, "b", "tones/y/tone.json");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ToneTrackErrorCode::SlicePositionOutsideRegion);
    }
    CHECK(track.regions.size() == 1); // unchanged on failure
}

TEST_CASE("sliceToneRegion rejects an unknown region", "[core][tone-edits]")
{
    ToneTrack track;
    const auto result = sliceToneRegion(
        track, "missing", ToneGridPosition{.measure = 2, .beat = 1}, "b", "tones/y/tone.json");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::RegionNotFound);
}

TEST_CASE(
    "deleteToneRegion extends the previous region over the removed span", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions = {
        makeRegion(
            "a",
            ToneGridPosition{.measure = 1, .beat = 1},
            ToneGridPosition{.measure = 3, .beat = 1}),
        makeRegion(
            "b",
            ToneGridPosition{.measure = 3, .beat = 1},
            ToneGridPosition{.measure = 5, .beat = 1}),
        makeRegion(
            "c",
            ToneGridPosition{.measure = 5, .beat = 1},
            ToneGridPosition{.measure = 7, .beat = 1}),
    };

    const auto result = deleteToneRegion(track, "b");

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].id == "a");
    CHECK(track.regions[0].end == ToneGridPosition{.measure = 5, .beat = 1}); // extended over b
    CHECK(track.regions[1].id == "c");
    CHECK(track.regions[1].start == ToneGridPosition{.measure = 5, .beat = 1});
}

TEST_CASE("deleteToneRegion on the first region extends the next backward", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions = {
        makeRegion(
            "a",
            ToneGridPosition{.measure = 1, .beat = 1},
            ToneGridPosition{.measure = 3, .beat = 1}),
        makeRegion(
            "b",
            ToneGridPosition{.measure = 3, .beat = 1},
            ToneGridPosition{.measure = 5, .beat = 1}),
    };

    const auto result = deleteToneRegion(track, "a");

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 1);
    CHECK(track.regions[0].id == "b");
    CHECK(track.regions[0].start == ToneGridPosition{.measure = 1, .beat = 1}); // extended back
    CHECK(track.regions[0].end == ToneGridPosition{.measure = 5, .beat = 1});
}

TEST_CASE("deleteToneRegion on the only region empties the track", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion(
        "a", ToneGridPosition{.measure = 1, .beat = 1}, ToneGridPosition{.measure = 5, .beat = 1}));

    const auto result = deleteToneRegion(track, "a");

    REQUIRE(result.has_value());
    CHECK(track.regions.empty());
}

TEST_CASE("deleteToneRegion rejects an unknown region", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion(
        "a", ToneGridPosition{.measure = 1, .beat = 1}, ToneGridPosition{.measure = 5, .beat = 1}));

    const auto result = deleteToneRegion(track, "missing");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::RegionNotFound);
    CHECK(track.regions.size() == 1);
}

} // namespace rock_hero::common::core
