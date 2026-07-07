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
        .start = start,
        .end = end,
        .tone_document_ref = std::move(tone_ref),
    };
}

} // namespace

TEST_CASE("createToneRegion splits the region containing the position", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion(
        "a", ToneGridPosition{.measure = 1, .beat = 1}, ToneGridPosition{.measure = 5, .beat = 1}));

    const auto result = createToneRegion(
        track, ToneGridPosition{.measure = 3, .beat = 1}, "b", "tones/y/tone.json");

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].id == "a"); // the earlier tone runs up to the new marker
    CHECK(track.regions[0].start == ToneGridPosition{.measure = 1, .beat = 1});
    CHECK(track.regions[0].end == ToneGridPosition{.measure = 3, .beat = 1});
    CHECK(track.regions[1].id == "b"); // the new region begins at the marker
    CHECK(track.regions[1].start == ToneGridPosition{.measure = 3, .beat = 1});
    CHECK(track.regions[1].end == ToneGridPosition{.measure = 5, .beat = 1});
    CHECK(track.regions[1].tone_document_ref == "tones/y/tone.json");
}

TEST_CASE("createToneRegion splits the correct region among several", "[core][tone-edits]")
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

    const auto result = createToneRegion(
        track, ToneGridPosition{.measure = 4, .beat = 1}, "c", "tones/y/tone.json");

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 3);
    CHECK(track.regions[1].id == "b");
    CHECK(track.regions[1].end == ToneGridPosition{.measure = 4, .beat = 1});
    CHECK(track.regions[2].id == "c");
    CHECK(track.regions[2].start == ToneGridPosition{.measure = 4, .beat = 1});
    CHECK(track.regions[2].end == ToneGridPosition{.measure = 5, .beat = 1});
}

TEST_CASE("createToneRegion rejects a position not inside any region", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion(
        "a", ToneGridPosition{.measure = 1, .beat = 1}, ToneGridPosition{.measure = 3, .beat = 1}));

    // A region start, a region end (both boundaries), and a position past the end are all invalid:
    // there is no region strictly containing them, so there is nothing to split.
    for (const ToneGridPosition at :
         {ToneGridPosition{.measure = 1, .beat = 1},
          ToneGridPosition{.measure = 3, .beat = 1},
          ToneGridPosition{.measure = 5, .beat = 1}})
    {
        const auto result = createToneRegion(track, at, "b", "tones/y/tone.json");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ToneTrackErrorCode::PositionOutsideAnyRegion);
    }
    CHECK(track.regions.size() == 1); // unchanged on failure
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

TEST_CASE("deleteToneRegion refuses to remove the only region", "[core][tone-edits]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion(
        "a", ToneGridPosition{.measure = 1, .beat = 1}, ToneGridPosition{.measure = 5, .beat = 1}));

    const auto result = deleteToneRegion(track, "a");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::CannotRemoveOnlyRegion);
    CHECK(track.regions.size() == 1); // the song must always stay covered
}

TEST_CASE("deleteToneRegion rejects an unknown region", "[core][tone-edits]")
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

    const auto result = deleteToneRegion(track, "missing");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::RegionNotFound);
    CHECK(track.regions.size() == 2);
}

} // namespace rock_hero::common::core
