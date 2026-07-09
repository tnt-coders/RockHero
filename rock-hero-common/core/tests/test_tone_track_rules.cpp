#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>

namespace rock_hero::common::core
{

namespace
{

constexpr std::string_view g_verse_region_id{"5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e"};
constexpr std::string_view g_chorus_region_id{"c9d8e7f6-a5b4-4c3d-9e2f-1a0b9c8d7e6f"};
constexpr std::string_view g_tone_ref{"tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json"};

// Builds a 4/4 map at 120 BPM whose terminal downbeat sits at measure 3 beat 1.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{4.0});
}

// Builds one valid region between the supplied whole-beat endpoints.
[[nodiscard]] ToneRegion makeRegion(std::string_view id, GridPosition start, GridPosition end)
{
    return ToneRegion{
        .id = std::string{id},
        .start = start,
        .end = end,
        .tone_document_ref = std::string{g_tone_ref},
    };
}

} // namespace

TEST_CASE("Tone track rules accept sorted whole-beat regions", "[core][tone]")
{
    const ToneTrack tone_track{
        .regions = {
            makeRegion(g_verse_region_id, {.measure = 1, .beat = 1}, {.measure = 2, .beat = 1}),
            makeRegion(g_chorus_region_id, {.measure = 2, .beat = 3}, {.measure = 3, .beat = 1}),
        },
    };

    CHECK(validateToneTrackRules(tone_track, makeTempoMap()).has_value());
}

TEST_CASE("Tone track rules reject overlapping regions", "[core][tone]")
{
    const ToneTrack tone_track{
        .regions = {
            makeRegion(g_verse_region_id, {.measure = 1, .beat = 1}, {.measure = 2, .beat = 3}),
            makeRegion(g_chorus_region_id, {.measure = 2, .beat = 1}, {.measure = 3, .beat = 1}),
        },
    };

    const auto result = validateToneTrackRules(tone_track, makeTempoMap());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::UnsortedOrOverlappingRegions);
}

TEST_CASE("Tone track rules reject reversed endpoints", "[core][tone]")
{
    const ToneTrack tone_track{
        .regions = {
            makeRegion(g_verse_region_id, {.measure = 2, .beat = 1}, {.measure = 1, .beat = 1}),
        },
    };

    const auto result = validateToneTrackRules(tone_track, makeTempoMap());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::EmptyOrReversedRegion);
}

TEST_CASE("Tone track rules reject regions past the terminal anchor", "[core][tone]")
{
    const ToneTrack tone_track{
        .regions = {
            makeRegion(g_verse_region_id, {.measure = 1, .beat = 1}, {.measure = 9, .beat = 1}),
        },
    };

    const auto result = validateToneTrackRules(tone_track, makeTempoMap());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::RegionPastTerminalAnchor);
}

TEST_CASE("Tone track rules reject invalid beats and ids and refs", "[core][tone]")
{
    ToneTrack tone_track{
        .regions = {
            makeRegion(g_verse_region_id, {.measure = 1, .beat = 5}, {.measure = 2, .beat = 1}),
        },
    };
    auto result = validateToneTrackRules(tone_track, makeTempoMap());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::InvalidEndpoint);

    tone_track.regions.front().start = GridPosition{.measure = 1, .beat = 1};
    tone_track.regions.front().id = "not-a-uuid";
    result = validateToneTrackRules(tone_track, makeTempoMap());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::InvalidRegionId);

    tone_track.regions.front().id = std::string{g_verse_region_id};
    tone_track.regions.front().tone_document_ref = "not/canonical.json";
    result = validateToneTrackRules(tone_track, makeTempoMap());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ToneTrackErrorCode::InvalidToneDocumentRef);
}

} // namespace rock_hero::common::core
