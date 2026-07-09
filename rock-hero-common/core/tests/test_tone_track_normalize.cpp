#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_normalize.h>
#include <string>

namespace rock_hero::common::core
{

namespace
{

// The default 4/4 map at 120 BPM over 4 seconds terminates on the measure-3 downbeat.
constexpr const char* g_default_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json";

[[nodiscard]] Song makeSong()
{
    Song song;
    song.tempo_map = TempoMap::defaultMap(TimeDuration{4.0});
    return song;
}

} // namespace

TEST_CASE("ensureExplicitToneRegions materializes the whole-song region", "[core][tone-normalize]")
{
    Song song = makeSong();
    Arrangement arrangement;
    arrangement.tones = {Tone{.tone_document_ref = g_default_ref, .name = "Clean Verse"}};
    song.arrangements.push_back(arrangement);

    ensureExplicitToneRegions(song);

    const Arrangement& normalized = song.arrangements.front();
    REQUIRE(normalized.tone_track.regions.size() == 1);
    const ToneRegion& region = normalized.tone_track.regions.front();
    CHECK_FALSE(region.id.empty());
    CHECK(region.start == GridPosition{.measure = 1, .beat = 1});
    CHECK(region.end == GridPosition{.measure = 3, .beat = 1});
    CHECK(region.tone_document_ref == g_default_ref);
    // The catalog itself is untouched: the region references the first tone, keeping its name.
    REQUIRE(normalized.tones.size() == 1);
    CHECK(normalized.tones.front().name == "Clean Verse");
}

TEST_CASE("ensureExplicitToneRegions leaves authored regions untouched", "[core][tone-normalize]")
{
    Song song = makeSong();
    Arrangement arrangement;
    arrangement.tones = {Tone{.tone_document_ref = g_default_ref, .name = "Default"}};
    arrangement.tone_track.regions = {
        ToneRegion{
            .id = "existing",
            .start = GridPosition{.measure = 1, .beat = 1},
            .end = GridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = g_default_ref,
        },
    };
    song.arrangements.push_back(arrangement);

    ensureExplicitToneRegions(song);

    const Arrangement& normalized = song.arrangements.front();
    REQUIRE(normalized.tone_track.regions.size() == 1);
    CHECK(normalized.tone_track.regions.front().id == "existing");
}

TEST_CASE(
    "ensureExplicitToneRegions leaves a tone-less arrangement empty", "[core][tone-normalize]")
{
    Song song = makeSong();
    song.arrangements.emplace_back(); // empty catalog: the load baseline mints before this runs

    ensureExplicitToneRegions(song);

    const Arrangement& normalized = song.arrangements.front();
    CHECK(normalized.tone_track.regions.empty());
    CHECK(normalized.tones.empty());
}

} // namespace rock_hero::common::core
