#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <string>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

constexpr const char* g_tone_x = "tones/x/tone.json";
constexpr const char* g_tone_y = "tones/y/tone.json";

// Builds a region for edit tests. Starts are not validated here; the edit functions operate
// purely on the region vector and leave grid/id validation to validateToneTrackRules.
[[nodiscard]] ToneRegion makeRegion(
    std::string id, GridPosition start, std::string tone_ref = g_tone_x)
{
    return ToneRegion{
        .id = std::move(id), .start = start, .tone_document_ref = std::move(tone_ref)
    };
}

[[nodiscard]] GridPosition at(int measure)
{
    return GridPosition{.measure = measure, .beat = 1};
}

// Three regions X, Y, X: the shape whose middle delete and whose retones exercise the merge law.
[[nodiscard]] ToneTrack makeAlternatingTrack()
{
    ToneTrack track;
    track.regions = {
        makeRegion("a", at(1), g_tone_x),
        makeRegion("b", at(3), g_tone_y),
        makeRegion("c", at(5), g_tone_x)
    };
    return track;
}

} // namespace

TEST_CASE("toneRegionAt names the last region starting at or before a position", "[core][tone]")
{
    const ToneTrack track = makeAlternatingTrack();

    CHECK(toneRegionAt(track, at(3))->id == "b"); // a start belongs to the region it opens
    CHECK(toneRegionAt(track, at(4))->id == "b"); // inside
    CHECK(toneRegionAt(track, at(9))->id == "c"); // past the last start: the last region
    CHECK(toneRegionAt(track, GridPosition{.measure = 1, .beat = 1}) != nullptr);

    ToneTrack late;
    late.regions = {makeRegion("a", at(2))};
    CHECK(toneRegionAt(late, at(1)) == nullptr); // before the first region: nowhere
}

TEST_CASE("coalesceToneRegions keeps the earlier of two regions on one tone", "[core][tone]")
{
    ToneTrack track;
    track.regions = {
        makeRegion("a", at(1), g_tone_x),
        makeRegion("b", at(3), g_tone_x),
        makeRegion("c", at(5), g_tone_y),
        makeRegion("d", at(7), g_tone_y),
        makeRegion("e", at(9), g_tone_x)
    };

    coalesceToneRegions(track);

    REQUIRE(track.regions.size() == 3);
    CHECK(track.regions[0].id == "a"); // b's boundary changed nothing, so it is gone
    CHECK(track.regions[1].id == "c");
    CHECK(track.regions[1].start == at(5));
    CHECK(track.regions[2].id == "e");
}

TEST_CASE("createToneRegion splits the region containing the position", "[core][tone]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion("a", at(1)));

    const auto result = createToneRegion(track, at(3), "b", g_tone_y);

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].id == "a"); // the earlier tone runs up to the new marker
    CHECK(track.regions[0].start == at(1));
    CHECK(track.regions[1].id == "b"); // the new region begins at the marker
    CHECK(track.regions[1].start == at(3));
    CHECK(track.regions[1].tone_document_ref == g_tone_y);
}

TEST_CASE("createToneRegion splits the correct region among several", "[core][tone]")
{
    ToneTrack track;
    track.regions = {makeRegion("a", at(1), g_tone_x), makeRegion("b", at(3), g_tone_y)};

    const auto result = createToneRegion(track, at(4), "c", g_tone_x);

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 3);
    CHECK(track.regions[1].id == "b");
    CHECK(track.regions[2].id == "c");
    CHECK(track.regions[2].start == at(4));
}

TEST_CASE("createToneRegion rejects a position on a region start", "[core][tone]")
{
    ToneTrack track;
    track.regions = {makeRegion("a", at(1), g_tone_x), makeRegion("b", at(3), g_tone_y)};

    // A region start is that region's own tone change; there is nothing to split there. A position
    // before the first region lies outside the track altogether.
    for (const GridPosition position : {at(1), at(3)})
    {
        const auto result = createToneRegion(track, position, "c", g_tone_x);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ToneTrackErrorCode::PositionOutsideAnyRegion);
    }
    CHECK(track.regions.size() == 2); // unchanged on failure
}

TEST_CASE("createToneRegion with the containing region's tone changes nothing", "[core][tone]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion("a", at(1), g_tone_x));

    REQUIRE(createToneRegion(track, at(3), "b", g_tone_x).has_value());

    // The boundary would have changed no tone, so it is no boundary: the track is as it was.
    REQUIRE(track.regions.size() == 1);
    CHECK(track.regions[0].id == "a");
}

TEST_CASE("createToneRegion with the next region's tone pulls that tone back", "[core][tone]")
{
    ToneTrack track;
    track.regions = {makeRegion("a", at(1), g_tone_x), makeRegion("b", at(3), g_tone_y)};

    REQUIRE(createToneRegion(track, at(2), "c", g_tone_y).has_value());

    // Y now begins at the marker; the boundary b opened changed nothing any more and is gone.
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].id == "a");
    CHECK(track.regions[1].id == "c");
    CHECK(track.regions[1].start == at(2));
    CHECK(track.regions[1].tone_document_ref == g_tone_y);
}

TEST_CASE("deleteToneRegion lets the previous region run on over the removed span", "[core][tone]")
{
    ToneTrack track;
    track.regions = {
        makeRegion("a", at(1), g_tone_x),
        makeRegion("b", at(3), g_tone_y),
        makeRegion("c", at(5), "tones/z/tone.json")
    };

    const auto result = deleteToneRegion(track, "b");

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].id == "a");
    CHECK(track.regions[1].id == "c");
    CHECK(track.regions[1].start == at(5)); // a runs on to here
}

TEST_CASE("deleteToneRegion merges the neighbors it brings together", "[core][tone]")
{
    ToneTrack track = makeAlternatingTrack();

    REQUIRE(deleteToneRegion(track, "b").has_value());

    // X, Y, X minus the middle is one X: c's boundary would change nothing, so it is gone too.
    REQUIRE(track.regions.size() == 1);
    CHECK(track.regions[0].id == "a");
    CHECK(track.regions[0].start == at(1));
    CHECK(track.regions[0].tone_document_ref == g_tone_x);
}

TEST_CASE("deleteToneRegion on the first region hands the song start to the next", "[core][tone]")
{
    ToneTrack track;
    track.regions = {makeRegion("a", at(1), g_tone_x), makeRegion("b", at(3), g_tone_y)};

    const auto result = deleteToneRegion(track, "a");

    REQUIRE(result.has_value());
    REQUIRE(track.regions.size() == 1);
    CHECK(track.regions[0].id == "b");
    CHECK(track.regions[0].start == at(1)); // extended back
}

TEST_CASE("deleteToneRegion refuses to remove the only region", "[core][tone]")
{
    ToneTrack track;
    track.regions.push_back(makeRegion("a", at(1)));

    const auto result = deleteToneRegion(track, "a");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::CannotRemoveOnlyRegion);
    CHECK(track.regions.size() == 1);
}

TEST_CASE("deleteToneRegion rejects an unknown region", "[core][tone]")
{
    ToneTrack track;
    track.regions = {makeRegion("a", at(1), g_tone_x), makeRegion("b", at(3), g_tone_y)};

    const auto result = deleteToneRegion(track, "missing");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::RegionNotFound);
    CHECK(track.regions.size() == 2);
}

TEST_CASE("retoneToneRegion repoints a region between other tones", "[core][tone]")
{
    ToneTrack track = makeAlternatingTrack();

    REQUIRE(retoneToneRegion(track, "b", "tones/z/tone.json").has_value());

    REQUIRE(track.regions.size() == 3);
    CHECK(track.regions[1].id == "b");
    CHECK(track.regions[1].tone_document_ref == "tones/z/tone.json");
}

TEST_CASE("retoneToneRegion onto the previous tone merges the region into it", "[core][tone]")
{
    ToneTrack track = makeAlternatingTrack();

    REQUIRE(retoneToneRegion(track, "b", g_tone_x).has_value());

    // b came to sound what a sounds, and so did c: one X across the song, under a's identity.
    REQUIRE(track.regions.size() == 1);
    CHECK(track.regions[0].id == "a");
    CHECK(track.regions[0].start == at(1));
}

TEST_CASE("retoneToneRegion onto the next tone takes the next region into it", "[core][tone]")
{
    ToneTrack track = makeAlternatingTrack();

    REQUIRE(retoneToneRegion(track, "a", g_tone_y).has_value());

    // a and b now share Y from the song start; c's X still opens a change and stands.
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].id == "a");
    CHECK(track.regions[0].tone_document_ref == g_tone_y);
    CHECK(track.regions[1].id == "c");
    CHECK(track.regions[1].start == at(5));
}

TEST_CASE("retoneToneRegion rejects an unknown region", "[core][tone]")
{
    ToneTrack track = makeAlternatingTrack();

    const auto result = retoneToneRegion(track, "missing", g_tone_y);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::RegionNotFound);
    CHECK(track.regions == makeAlternatingTrack().regions);
}

TEST_CASE("moveToneBoundary moves a region's start between its neighbors", "[core][tone]")
{
    ToneTrack track = makeAlternatingTrack();

    REQUIRE(moveToneBoundary(track, "b", at(2)).has_value());
    CHECK(track.regions[1].start == at(2));

    REQUIRE(moveToneBoundary(track, "c", at(8)).has_value()); // the last has no next to bound it
    CHECK(track.regions[2].start == at(8));
}

TEST_CASE("moveToneBoundary refuses the song start and a crossed neighbor", "[core][tone]")
{
    ToneTrack track = makeAlternatingTrack();

    const auto first = moveToneBoundary(track, "a", at(2));
    REQUIRE_FALSE(first.has_value());
    CHECK(first.error().code == ToneTrackErrorCode::CannotMoveSongStart);

    // Onto or past a neighbor's start, either side, would empty or reorder a region.
    for (const GridPosition position : {at(1), at(5), at(6)})
    {
        const auto crossed = moveToneBoundary(track, "b", position);
        REQUIRE_FALSE(crossed.has_value());
        CHECK(crossed.error().code == ToneTrackErrorCode::UnsortedOrOverlappingRegions);
    }

    const auto unknown = moveToneBoundary(track, "missing", at(2));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().code == ToneTrackErrorCode::RegionNotFound);

    CHECK(track.regions == makeAlternatingTrack().regions); // unchanged on every refusal
}

} // namespace rock_hero::common::core
