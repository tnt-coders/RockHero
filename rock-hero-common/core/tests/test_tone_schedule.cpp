#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_schedule.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Builds a region on the default 120 BPM 4/4 map, where measure N beat 1 lands at (N-1)*2.0 s.
[[nodiscard]] ToneRegion makeRegion(
    int start_measure, int end_measure, const std::string& tone_document_ref)
{
    return ToneRegion{
        .id = tone_document_ref + "-region",
        .start = GridPosition{.measure = start_measure, .beat = 1, .offset = {}},
        .end = GridPosition{.measure = end_measure, .beat = 1, .offset = {}},
        .tone_document_ref = tone_document_ref,
    };
}

// Asserts a baked envelope point field-by-field. The gain is compared with tolerance so the raw
// float member never runs through ToneGainPoint's exact ==, which -Wfloat-equal rejects.
void checkGainPoint(const ToneGainPoint& point, double seconds, float gain)
{
    CHECK(point.seconds == Catch::Approx(seconds));
    CHECK_THAT(point.gain, Catch::Matchers::WithinULP(gain, 0));
}

} // namespace

// Verifies an empty tone track produces an empty schedule: there are no tones to switch.
TEST_CASE("Tone schedule of an empty track is empty", "[core][tone-schedule]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{8.0});

    const auto schedule = makeToneSchedule(ToneTrack{}, tempo_map, TimeDuration{8.0});

    CHECK(schedule.empty());
}

// Verifies the first region owns the lead-in (extends to the timeline origin) and the last
// region extends to the end of the loaded content.
TEST_CASE("Tone schedule extends head to origin and tail to song end", "[core][tone-schedule]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{16.0});
    ToneTrack track;
    track.regions.push_back(makeRegion(2, 3, "tones/a/tone.json"));

    const auto schedule = makeToneSchedule(track, tempo_map, TimeDuration{16.0});

    REQUIRE(schedule.size() == 1);
    CHECK(schedule.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(schedule.front().time_range.end.seconds == Catch::Approx(16.0));
    CHECK(schedule.front().tone_document_ref == "tones/a/tone.json");
}

// Verifies a gap between authored regions holds the previous tone: each span ends where the
// next begins, never at its own authored end.
TEST_CASE("Tone schedule gap holds the previous tone", "[core][tone-schedule]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{16.0});
    ToneTrack track;
    // Authored end at measure 2 (2.0 s), but the next region starts at measure 5 (8.0 s): the
    // gap between 2.0 s and 8.0 s must stay on tone a.
    track.regions.push_back(makeRegion(1, 2, "tones/a/tone.json"));
    track.regions.push_back(makeRegion(5, 7, "tones/b/tone.json"));

    const auto schedule = makeToneSchedule(track, tempo_map, TimeDuration{16.0});

    REQUIRE(schedule.size() == 2);
    CHECK(schedule[0].time_range.start.seconds == Catch::Approx(0.0));
    CHECK(schedule[0].time_range.end.seconds == Catch::Approx(8.0));
    CHECK(schedule[1].time_range.start.seconds == Catch::Approx(8.0));
    CHECK(schedule[1].time_range.end.seconds == Catch::Approx(16.0));
    CHECK(schedule[1].tone_document_ref == "tones/b/tone.json");
}

// Verifies the terminal clamp: an authored end past the loaded content clamps to the content
// length, and a region starting at or past the content still yields a forward (empty) span.
TEST_CASE("Tone schedule clamps the terminal span to content length", "[core][tone-schedule]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{16.0});
    ToneTrack track;
    track.regions.push_back(makeRegion(1, 9, "tones/a/tone.json"));

    const auto schedule = makeToneSchedule(track, tempo_map, TimeDuration{6.5});

    REQUIRE(schedule.size() == 1);
    CHECK(schedule.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(schedule.front().time_range.end.seconds == Catch::Approx(6.5));
}

// Verifies contiguity across several regions: every span's end equals the next span's start, so
// downstream automation baking never sees overlap or uncovered time.
TEST_CASE("Tone schedule spans are contiguous", "[core][tone-schedule]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{16.0});
    ToneTrack track;
    track.regions.push_back(makeRegion(1, 2, "tones/a/tone.json"));
    track.regions.push_back(makeRegion(3, 4, "tones/b/tone.json"));
    track.regions.push_back(makeRegion(6, 8, "tones/a/tone.json"));

    const auto schedule = makeToneSchedule(track, tempo_map, TimeDuration{16.0});

    REQUIRE(schedule.size() == 3);
    for (std::size_t index = 0; index + 1 < schedule.size(); ++index)
    {
        CHECK(
            schedule[index].time_range.end.seconds ==
            Catch::Approx(schedule[index + 1].time_range.start.seconds));
    }
    CHECK(schedule.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(schedule.back().time_range.end.seconds == Catch::Approx(16.0));
}

// Verifies each tone's envelope: opening value, crossfade point pairs at switches, and the
// mirrored incoming/outgoing shapes.
TEST_CASE("Tone gain envelope bakes mirrored crossfades", "[core][tone-schedule]")
{
    const std::vector<ToneSwitchRegion> schedule{
        ToneSwitchRegion{
            .time_range = {.start = TimePosition{0.0}, .end = TimePosition{4.0}},
            .tone_document_ref = "tones/a/tone.json",
        },
        ToneSwitchRegion{
            .time_range = {.start = TimePosition{4.0}, .end = TimePosition{8.0}},
            .tone_document_ref = "tones/b/tone.json",
        },
    };

    const auto envelope_a = makeToneGainEnvelope(schedule, "tones/a/tone.json", 0.01);
    REQUIRE(envelope_a.size() == 3);
    checkGainPoint(envelope_a[0], 0.0, 1.0F);
    checkGainPoint(envelope_a[1], 4.0, 1.0F);
    checkGainPoint(envelope_a[2], 4.01, 0.0F);

    const auto envelope_b = makeToneGainEnvelope(schedule, "tones/b/tone.json", 0.01);
    REQUIRE(envelope_b.size() == 3);
    checkGainPoint(envelope_b[0], 0.0, 0.0F);
    checkGainPoint(envelope_b[1], 4.0, 0.0F);
    checkGainPoint(envelope_b[2], 4.01, 1.0F);
}

// Verifies a boundary between two spans of the SAME tone bakes nothing (no dip to silence) and
// a never-referenced tone yields exactly one silent origin point.
TEST_CASE("Tone gain envelope skips same-tone boundaries", "[core][tone-schedule]")
{
    const std::vector<ToneSwitchRegion> schedule{
        ToneSwitchRegion{
            .time_range = {.start = TimePosition{0.0}, .end = TimePosition{4.0}},
            .tone_document_ref = "tones/a/tone.json",
        },
        ToneSwitchRegion{
            .time_range = {.start = TimePosition{4.0}, .end = TimePosition{8.0}},
            .tone_document_ref = "tones/a/tone.json",
        },
    };

    const auto envelope_a = makeToneGainEnvelope(schedule, "tones/a/tone.json", 0.01);
    REQUIRE(envelope_a.size() == 1);
    checkGainPoint(envelope_a.front(), 0.0, 1.0F);

    const auto envelope_c = makeToneGainEnvelope(schedule, "tones/c/tone.json", 0.01);
    REQUIRE(envelope_c.size() == 1);
    checkGainPoint(envelope_c.front(), 0.0, 0.0F);
}

// Verifies the crossfade clamps to half the incoming span so back-to-back short spans cannot
// overlap their fades.
TEST_CASE("Tone gain envelope clamps ramps to short spans", "[core][tone-schedule]")
{
    const std::vector<ToneSwitchRegion> schedule{
        ToneSwitchRegion{
            .time_range = {.start = TimePosition{0.0}, .end = TimePosition{1.0}},
            .tone_document_ref = "tones/a/tone.json",
        },
        ToneSwitchRegion{
            .time_range = {.start = TimePosition{1.0}, .end = TimePosition{1.01}},
            .tone_document_ref = "tones/b/tone.json",
        },
    };

    const auto envelope_b = makeToneGainEnvelope(schedule, "tones/b/tone.json", 0.01);
    REQUIRE(envelope_b.size() == 3);
    checkGainPoint(envelope_b[2], 1.005, 1.0F);
}

} // namespace rock_hero::common::core
