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
[[nodiscard]] ToneRegion makeRegion(int start_measure, const std::string& tone_document_ref)
{
    return ToneRegion{
        .id = tone_document_ref + "-region",
        .start = GridPosition{.measure = start_measure, .beat = 1, .offset = {}},
        .tone_document_ref = tone_document_ref,
    };
}

// Asserts a baked envelope point field-by-field; ToneGainPoint deliberately has no operator== of
// its own (two raw float members), so field probes are the whole interface.
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
    track.regions.push_back(makeRegion(2, "tones/a/tone.json"));

    const auto schedule = makeToneSchedule(track, tempo_map, TimeDuration{16.0});

    REQUIRE(schedule.size() == 1);
    CHECK(schedule.front().time_range.start.seconds == Catch::Approx(0.0));
    CHECK(schedule.front().time_range.end.seconds == Catch::Approx(16.0));
    CHECK(schedule.front().tone_document_ref == "tones/a/tone.json");
}

// Verifies a region holds its tone all the way to the next region's start: a region stores only
// where it begins, so the whole stretch up to the next tone change stays on the earlier tone.
TEST_CASE("Tone schedule holds a tone up to the next region's start", "[core][tone-schedule]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{16.0});
    ToneTrack track;
    // Tone a opens at measure 1 and the next change is at measure 5 (8.0 s), so everything up to
    // 8.0 s must stay on tone a.
    track.regions.push_back(makeRegion(1, "tones/a/tone.json"));
    track.regions.push_back(makeRegion(5, "tones/b/tone.json"));

    const auto schedule = makeToneSchedule(track, tempo_map, TimeDuration{16.0});

    REQUIRE(schedule.size() == 2);
    CHECK(schedule[0].time_range.start.seconds == Catch::Approx(0.0));
    CHECK(schedule[0].time_range.end.seconds == Catch::Approx(8.0));
    CHECK(schedule[1].time_range.start.seconds == Catch::Approx(8.0));
    CHECK(schedule[1].time_range.end.seconds == Catch::Approx(16.0));
    CHECK(schedule[1].tone_document_ref == "tones/b/tone.json");
}

// Verifies the terminal clamp: the last span runs to the end of the loaded content, which may be
// shorter than the tempo map, and never past it.
TEST_CASE("Tone schedule clamps the terminal span to content length", "[core][tone-schedule]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{16.0});
    ToneTrack track;
    track.regions.push_back(makeRegion(1, "tones/a/tone.json"));

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
    track.regions.push_back(makeRegion(1, "tones/a/tone.json"));
    track.regions.push_back(makeRegion(3, "tones/b/tone.json"));
    track.regions.push_back(makeRegion(6, "tones/a/tone.json"));

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
    REQUIRE(envelope_a.size() == 4);
    checkGainPoint(envelope_a[0], 0.0, 1.0F);
    checkGainPoint(envelope_a[1], 4.0, 1.0F);
    checkGainPoint(envelope_a[2], 4.01, 0.0F);
    checkGainPoint(envelope_a[3], 8.0, 0.0F);

    const auto envelope_b = makeToneGainEnvelope(schedule, "tones/b/tone.json", 0.01);
    REQUIRE(envelope_b.size() == 4);
    checkGainPoint(envelope_b[0], 0.0, 0.0F);
    checkGainPoint(envelope_b[1], 4.0, 0.0F);
    checkGainPoint(envelope_b[2], 4.01, 1.0F);
    checkGainPoint(envelope_b[3], 8.0, 1.0F);
}

// Verifies the closing anchor every envelope carries: a point at the schedule's end repeating the
// gain already reached. It exists so no branch ever reaches Tracktion with a single point, which is
// discarded as "not automated" and is rewritten in place by an ordinary gain write.
TEST_CASE("Tone gain envelope closes on the schedule end", "[core][tone-schedule]")
{
    const std::vector<ToneSwitchRegion> schedule{
        ToneSwitchRegion{
            .time_range = {.start = TimePosition{0.0}, .end = TimePosition{6.5}},
            .tone_document_ref = "tones/a/tone.json",
        },
    };

    // One region, no boundary to cross: the opening and the closing anchor are the whole envelope.
    const auto envelope_a = makeToneGainEnvelope(schedule, "tones/a/tone.json", 0.01);
    REQUIRE(envelope_a.size() == 2);
    checkGainPoint(envelope_a[0], 0.0, 1.0F);
    checkGainPoint(envelope_a[1], 6.5, 1.0F);

    // An empty schedule has no end to close on, so both anchors sit silent at the origin — still
    // two points, which is the property the backend depends on.
    const auto envelope_none = makeToneGainEnvelope({}, "tones/a/tone.json", 0.01);
    REQUIRE(envelope_none.size() == 2);
    checkGainPoint(envelope_none[0], 0.0, 0.0F);
    checkGainPoint(envelope_none[1], 0.0, 0.0F);
}

// Verifies a boundary between two spans of the SAME tone bakes nothing (no dip to silence) and
// a never-referenced tone yields a flat silent envelope rather than a lone point.
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
    REQUIRE(envelope_a.size() == 2);
    checkGainPoint(envelope_a[0], 0.0, 1.0F);
    checkGainPoint(envelope_a[1], 8.0, 1.0F);

    const auto envelope_c = makeToneGainEnvelope(schedule, "tones/c/tone.json", 0.01);
    REQUIRE(envelope_c.size() == 2);
    checkGainPoint(envelope_c[0], 0.0, 0.0F);
    checkGainPoint(envelope_c[1], 8.0, 0.0F);
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
    REQUIRE(envelope_b.size() == 4);
    checkGainPoint(envelope_b[2], 1.005, 1.0F);
    checkGainPoint(envelope_b[3], 1.01, 1.0F);
}

} // namespace rock_hero::common::core
