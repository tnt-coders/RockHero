#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

using Catch::Matchers::WithinULP;
using rock_hero::common::core::testing::anchor;
using rock_hero::common::core::testing::commonTimeMap;

// Three sparse 4/4 anchors shared by the interpolation, cursor, and tempo-query cases: eight
// beats at 0.5s each across the first span, then four beats at 0.375s each, so queries exercise
// two different anchor-span rates plus the 10-second offset start.
[[nodiscard]] TempoMap makeSparseAnchor44Map()
{
    return commonTimeMap({
        anchor("1:1", 10000),
        anchor("3:1", 14000),
        anchor("4:1", 15500),
    });
}

// A 4/4 map with an anchor between beats 3 and 4 of measure 1: two and a half beats in the first
// second, one and a half in the next.
[[nodiscard]] TempoMap makeSubBeatAnchorMap()
{
    return commonTimeMap({
        anchor("1:1", 0),
        anchor("1:3+1/2", 1000),
        anchor("2:1", 2000),
    });
}

} // namespace

// Verifies the draft tempo map is a valid one-measure 4/4 grid at 120 BPM.
TEST_CASE("TempoMap default construction creates a usable grid", "[core][tempo-map]")
{
    const TempoMap tempo_map;

    REQUIRE(tempo_map.timeSignatures().size() == 1);
    REQUIRE(tempo_map.anchors().size() == 2);
    CHECK(tempo_map.timeSignatures().front() == TimeSignatureChange{});
    CHECK(tempo_map.anchors().front() == anchor("1:1", 0));
    CHECK(tempo_map.anchors().back() == anchor("2:1", 2000));
    CHECK(tempo_map.secondsAtBeat(1, 3) == Catch::Approx(1.0));
}

// Verifies global beat indices account for carried-forward time-signature changes.
TEST_CASE("TempoMap indexes beats across meter changes", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 3, .meter = {.numerator = 3, .denominator = 4}},
            TimeSignatureChange{.measure = 5, .meter = {.numerator = 5, .denominator = 4}},
        },
        std::vector{
            anchor("1:1", 0),
            anchor("6:1", 9500),
        },
    };

    CHECK(tempo_map.globalBeatIndex(1, 1) == 0);
    CHECK(tempo_map.globalBeatIndex(2, 1) == 4);
    CHECK(tempo_map.globalBeatIndex(3, 1) == 8);
    CHECK(tempo_map.globalBeatIndex(4, 3) == 13);
    CHECK(tempo_map.globalBeatIndex(5, 1) == 14);
    CHECK(tempo_map.beatAtGlobalIndex(13) == std::pair{4, 3});
    CHECK(tempo_map.beatAtGlobalIndex(14) == std::pair{5, 1});
}

// Verifies sparse anchors resolve intermediate beats and fractional note offsets.
TEST_CASE("TempoMap interpolates sparse anchors", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSparseAnchor44Map();

    CHECK(tempo_map.secondsAtBeat(2, 1) == Catch::Approx(12.0));
    CHECK(
        tempo_map.secondsAt(GridPosition{.measure = 2, .beat = 2, .offset = Fraction{1, 2}}) ==
        Catch::Approx(12.75));
    CHECK(tempo_map.terminalGlobalBeatIndex() == 12);
}

// Verifies the shared global-beat interpolation query matches measure-address lookups and clamps
// outside the authored anchor range.
TEST_CASE("TempoMap resolves fractional global beat positions", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSparseAnchor44Map();

    CHECK(tempo_map.secondsAtGlobalBeatPosition(4.0) == Catch::Approx(12.0));
    CHECK(tempo_map.secondsAtGlobalBeatPosition(5.5) == Catch::Approx(12.75));
    CHECK(tempo_map.secondsAtGlobalBeatPosition(10.0) == Catch::Approx(14.75));
    CHECK(tempo_map.secondsAtGlobalBeatPosition(-1.0) == Catch::Approx(10.0));
    CHECK(tempo_map.secondsAtGlobalBeatPosition(99.0) == Catch::Approx(15.5));
}

// Verifies the forward cursor resolves monotonic positions identically to the binary-search
// query, including the clamped edges.
TEST_CASE("TempoMap forward cursor matches the global-beat query", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSparseAnchor44Map();

    TempoMap::ForwardBeatTimeCursor cursor{tempo_map};
    for (const double position : {-1.0, 0.0, 2.5, 4.0, 8.0, 10.0, 12.0, 99.0})
    {
        CHECK(
            std::is_eq(
                cursor.secondsAt(position) <=> tempo_map.secondsAtGlobalBeatPosition(position)));
    }
}

// Verifies anchor interpolation stays aligned with the meter-aware beat axis across signature
// changes, exercising the derived segment and anchor index tables together.
TEST_CASE("TempoMap interpolates across meter changes", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 3, .meter = {.numerator = 3, .denominator = 4}},
            TimeSignatureChange{.measure = 5, .meter = {.numerator = 5, .denominator = 4}},
        },
        std::vector{
            anchor("1:1", 0),
            anchor("6:1", 9500),
        },
    };

    // 19 beats span the anchors (4 + 4 + 3 + 3 + 5), so each beat lasts 0.5 seconds.
    CHECK(tempo_map.terminalGlobalBeatIndex() == 19);
    CHECK(tempo_map.secondsAtBeat(3, 1) == Catch::Approx(4.0));
    CHECK(
        tempo_map.secondsAt(GridPosition{.measure = 5, .beat = 1, .offset = Fraction{1, 2}}) ==
        Catch::Approx(7.25));
    CHECK(tempo_map.secondsAtGlobalBeatPosition(14.5) == Catch::Approx(7.25));
}

// Verifies each sparse anchor span is measured in quarters between its own two anchors: the first
// span holds 8 quarters over 4 seconds, the second 4 quarters over 1.5 seconds.
TEST_CASE("TempoMap measures each sparse anchor span in quarters", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSparseAnchor44Map();

    CHECK(tempo_map.spanQuarterLength(0) == Catch::Approx(8.0));
    CHECK(tempo_map.spanQuarterLength(1) == Catch::Approx(4.0));
    CHECK(tempo_map.secondsAtBeat(3, 1) - tempo_map.secondsAtBeat(1, 1) == Catch::Approx(4.0));
    CHECK(tempo_map.secondsAtBeat(4, 1) - tempo_map.secondsAtBeat(3, 1) == Catch::Approx(1.5));
}

// Verifies the quarter-note reference scales by the signature denominator: a 6/8 measure holds
// three quarter notes, so 120 eighth-note beats per minute are 60 quarter notes per minute.
TEST_CASE("TempoMap counts an eighth-note meter in quarter notes", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 6, .denominator = 8}},
        },
        std::vector{
            anchor("1:1", 0),
            anchor("2:1", 3000),
        },
    };

    CHECK(tempo_map.spanQuarterLength(0) == Catch::Approx(3.0));
    CHECK(tempo_map.secondsAtBeat(2, 1) == Catch::Approx(3.0));
}

// Verifies the seconds-to-beat inverse mirrors the forward query, including clamping outside the
// authored anchor range and crossing an un-anchored denominator change.
TEST_CASE("TempoMap resolves beat positions from seconds", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 2, .meter = {.numerator = 7, .denominator = 8}},
        },
        std::vector{
            anchor("1:1", 0),
            anchor("3:1", 3750),
        },
    };

    CHECK(tempo_map.beatPositionAtSeconds(0.5) == Catch::Approx(1.0));
    CHECK(tempo_map.beatPositionAtSeconds(2.25) == Catch::Approx(5.0));
    CHECK(tempo_map.beatPositionAtSeconds(-1.0) == Catch::Approx(0.0));
    CHECK(tempo_map.beatPositionAtSeconds(99.0) == Catch::Approx(11.0));
}

// Verifies the whole-note axis reaches the clock and back: a place resolves to the time its grid
// position does, a time resolves to the place it stands at, and both clamp outside the anchors.
TEST_CASE("TempoMap resolves whole-note places to seconds and back", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 2, .meter = {.numerator = 7, .denominator = 8}},
        },
        std::vector{
            anchor("1:1", 0),
            anchor("3:1", 3750),
        },
    };

    // A whole 4/4 bar then three eighths into the 7/8 bar: five and a half quarters, 2.75 s.
    const Fraction place{11, 8};
    CHECK(tempo_map.gridPositionAtWholeNote(place) == GridPosition{.measure = 2, .beat = 4});
    CHECK(tempo_map.quarterPositionAt(GridPosition{.measure = 2, .beat = 4}) == Catch::Approx(5.5));
    CHECK(tempo_map.secondsAtWholeNote(place) == Catch::Approx(2.75));
    CHECK_THAT(
        tempo_map.secondsAtWholeNote(place),
        Catch::Matchers::WithinULP(tempo_map.secondsAt(GridPosition{.measure = 2, .beat = 4}), 0));
    CHECK(tempo_map.wholeNotePositionAtSeconds(2.75) == Catch::Approx(11.0 / 8.0));
    CHECK(tempo_map.wholeNotePositionAtSeconds(-1.0) == Catch::Approx(0.0));
    CHECK(tempo_map.wholeNotePositionAtSeconds(99.0) == Catch::Approx(15.0 / 8.0));
}

// Verifies interpolation is linear in metronome time: an un-anchored denominator change keeps the
// quarter-note tempo constant, so eighth-note beats after a 4/4 -> 7/8 change run twice as fast
// instead of stretching to quarter-note length.
TEST_CASE("TempoMap holds quarter tempo across un-anchored meter changes", "[core][tempo-map]")
{
    // One span pins measure 3's downbeat: 4 quarters (4/4) plus 7 eighths (7/8) is 7.5 quarter
    // notes, which at 120 quarter notes per minute lands at 3.75 seconds.
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 2, .meter = {.numerator = 7, .denominator = 8}},
        },
        std::vector{
            anchor("1:1", 0),
            anchor("3:1", 3750),
        },
    };

    CHECK(tempo_map.spanQuarterLength(0) == Catch::Approx(7.5));
    CHECK(tempo_map.secondsAtBeat(1, 2) == Catch::Approx(0.5));
    CHECK(tempo_map.secondsAtBeat(2, 1) == Catch::Approx(2.0));
    CHECK(tempo_map.secondsAtBeat(2, 2) == Catch::Approx(2.25));
    CHECK(tempo_map.secondsAtBeat(3, 1) == Catch::Approx(3.75));
}

// Verifies signature lookup by absolute time follows the meter-change downbeats and clamps
// outside the authored range.
TEST_CASE("TempoMap resolves the active signature at a time", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 3, .meter = {.numerator = 3, .denominator = 4}},
            TimeSignatureChange{.measure = 5, .meter = {.numerator = 5, .denominator = 4}},
        },
        std::vector{
            anchor("1:1", 0),
            anchor("6:1", 9500),
        },
    };

    // 19 beats over 9.5 seconds puts measure 3 at 4.0s and measure 5 at 7.0s.
    CHECK(tempo_map.timeSignatureAtSeconds(3.9).meter.numerator == 4);
    CHECK(tempo_map.timeSignatureAtSeconds(4.0).meter.numerator == 3);
    CHECK(tempo_map.timeSignatureAtSeconds(7.2).meter.numerator == 5);
    CHECK(tempo_map.timeSignatureAtSeconds(-1.0).meter.numerator == 4);
    CHECK(tempo_map.timeSignatureAtSeconds(99.0).meter.numerator == 5);
}

// Verifies the generated fallback map extends to a terminal downbeat after the audio.
TEST_CASE("TempoMap defaultMap covers audio duration", "[core][tempo-map]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{5.1});

    REQUIRE(tempo_map.anchors().size() == 2);
    CHECK(tempo_map.anchors().back() == anchor("4:1", 6000));
}

// Verifies beat positions below a first anchor that is not measure 1 beat 1 clamp to that
// anchor's time instead of extrapolating the first span backwards, so the forward map agrees
// with beatPositionAtSeconds' inverse clamp. Package validation forbids such maps, but the
// value constructor accepts them and the documented contract still applies.
TEST_CASE("TempoMap clamps beat positions below the first anchor", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
        },
        std::vector{
            anchor("2:1", 10000),
            anchor("3:1", 14000),
        },
    };

    CHECK(tempo_map.secondsAtBeat(1, 1) == Catch::Approx(10.0));
    CHECK(tempo_map.secondsAtGlobalBeatPosition(2.0) == Catch::Approx(10.0));

    TempoMap::ForwardBeatTimeCursor cursor{tempo_map};
    CHECK(cursor.secondsAt(2.0) == Catch::Approx(10.0));

    // Positions inside the authored span still interpolate, and the inverse clamps to the first
    // anchor's beat index as before.
    CHECK(tempo_map.secondsAtBeat(2, 3) == Catch::Approx(12.0));
    CHECK(tempo_map.beatPositionAtSeconds(9.0) == Catch::Approx(4.0));
}

// Verifies a span's quarter length follows the meters inside it: 6/8 holds three quarters.
TEST_CASE("TempoMap reports each anchor span's quarter length", "[core][tempo-map]")
{
    const TempoMap map{
        {TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
         TimeSignatureChange{.measure = 2, .meter = {.numerator = 6, .denominator = 8}}},
        {anchor("1:1", 0), anchor("2:1", 2000), anchor("3:1", 3500)}
    };

    CHECK(map.spanQuarterLength(0) == Catch::Approx(4.0));
    CHECK(map.spanQuarterLength(1) == Catch::Approx(3.0));
    CHECK(map.spanQuarterLength(2) == Catch::Approx(0.0));
}

// Verifies a span lookup finds only an anchor that opens a span: the terminal opens none, and a
// beat no anchor stands on answers nothing.
TEST_CASE("TempoMap finds the span an anchor at a beat opens", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSparseAnchor44Map();

    const auto at = [](const int measure) {
        return GridPosition{.measure = measure, .beat = 1, .offset = {}};
    };
    CHECK(tempo_map.spanStartingAt(at(1)) == std::optional<std::size_t>{0});
    CHECK(tempo_map.spanStartingAt(at(3)) == std::optional<std::size_t>{1});
    CHECK_FALSE(tempo_map.spanStartingAt(at(4)).has_value());
    CHECK_FALSE(tempo_map.spanStartingAt(at(2)).has_value());
}

// Verifies the default map ends on the first downbeat at or past the audio, one measure at least.
TEST_CASE("TempoMap default map ends on the first downbeat covering the audio", "[core][tempo-map]")
{
    const auto terminal_of = [](const double audio_seconds) {
        return TempoMap::defaultMap(TimeDuration{audio_seconds}).anchors().back();
    };

    CHECK(terminal_of(0.0) == anchor("2:1", 2000));
    CHECK(terminal_of(8.0) == anchor("5:1", 8000));
    CHECK(terminal_of(3.9996) == anchor("3:1", 4000));
    CHECK(terminal_of(4.0004) == anchor("4:1", 6000));
    CHECK(terminal_of(93.5) == anchor("48:1", 94000));
}

// Verifies the covering terminal walks a meter change measure by measure, times every candidate
// from the anchor before it, and never moves a terminal that already covers the time.
TEST_CASE("TempoMap grows its terminal to cover a time", "[core][tempo-map]")
{
    const TempoMap map{
        {TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
         TimeSignatureChange{.measure = 3, .meter = {.numerator = 6, .denominator = 8}}},
        {anchor("1:1", 0), anchor("2:1", 2000)}
    };

    CHECK(map.terminalCovering(5.0, 0.5) == anchor("4:1", 5500));
    CHECK(map.terminalCovering(1.0, 0.5) == map.anchors().back());

    // 241 quarter notes a minute over five minutes: one rounding, not one per measure.
    const TempoMap fast{
        {TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}}},
        {anchor("1:1", 0), anchor("2:1", 996)}
    };
    const BeatAnchor grown = fast.terminalCovering(300.0, 60.0 / 241.0);
    const double quarters = static_cast<double>(grown.position.measure - 1) * 4.0;
    CHECK(grown.milliseconds == nearestAnchorMilliseconds(quarters * (60.0 / 241.0)));
    CHECK(grown.seconds() >= 300.0);
    CHECK(grown.seconds() - 4.0 * 60.0 / 241.0 < 300.0);
}

// Verifies an anchor compares as a plain value: both fields decide, the offset included.
TEST_CASE("BeatAnchor equality compares position and time", "[core][tempo-map]")
{
    const BeatAnchor pinned{
        .position = GridPosition{.measure = 2, .beat = 3, .offset = Fraction{1, 2}},
        .milliseconds = 3250,
    };
    const BeatAnchor floored{
        .position = GridPosition{.measure = 2, .beat = 3, .offset = {}},
        .milliseconds = 3250,
    };
    const BeatAnchor later{
        .position = GridPosition{.measure = 2, .beat = 3, .offset = Fraction{1, 2}},
        .milliseconds = 3251,
    };

    CHECK(pinned == anchor("2:3+1/2", 3250));
    CHECK_FALSE(pinned == floored);
    CHECK_FALSE(pinned == later);
    CHECK_THAT(pinned.seconds(), WithinULP(3.25, std::uint64_t{0}));
}

// Verifies a sub-beat anchor pins its own position: each span interpolates on its own side of
// it, the anchor's time comes back exactly, and the terminal still counts whole beats.
TEST_CASE("TempoMap interpolates around a sub-beat anchor", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSubBeatAnchorMap();
    const GridPosition between{.measure = 1, .beat = 3, .offset = Fraction{1, 2}};

    CHECK_THAT(tempo_map.secondsAtBeat(1, 2), WithinULP(0.4, std::uint64_t{1}));
    CHECK_THAT(tempo_map.secondsAt(between), WithinULP(1.0, std::uint64_t{0}));
    CHECK_THAT(tempo_map.secondsAtBeat(1, 4), WithinULP(4.0 / 3.0, std::uint64_t{2}));
    CHECK_THAT(tempo_map.beatPositionAtSeconds(1.0), WithinULP(2.5, std::uint64_t{0}));
    CHECK(tempo_map.spanStartingAt(between) == std::optional<std::size_t>{1});
    CHECK_FALSE(
        tempo_map.spanStartingAt(GridPosition{.measure = 1, .beat = 3, .offset = {}}).has_value());
    CHECK(tempo_map.terminalGlobalBeatIndex() == 4);
}

// Verifies the time-to-beat inverse finds a query's span by comparing in seconds: a time between
// two whole milliseconds still lands inside the first span instead of clamping to its start.
TEST_CASE("TempoMap resolves a time between two milliseconds", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSubBeatAnchorMap();

    CHECK_THAT(tempo_map.beatPositionAtSeconds(0.6005), WithinULP(1.50125, std::uint64_t{4}));
    CHECK_THAT(tempo_map.beatPositionAtSeconds(1.5), WithinULP(3.25, std::uint64_t{4}));
}

// Verifies the forward cursor agrees with the binary-search query at every tick across the
// sub-beat anchor, the span boundary the two must pick identically.
TEST_CASE("TempoMap forward cursor matches the query around a sub-beat anchor", "[core][tempo-map]")
{
    const TempoMap tempo_map = makeSubBeatAnchorMap();
    constexpr int ticks_per_beat = 960;

    TempoMap::ForwardBeatTimeCursor cursor{tempo_map};
    for (int tick = 2 * ticks_per_beat; tick <= 3 * ticks_per_beat; ++tick)
    {
        const double position = static_cast<double>(tick) / static_cast<double>(ticks_per_beat);
        CHECK(
            std::is_eq(
                cursor.secondsAt(position) <=> tempo_map.secondsAtGlobalBeatPosition(position)));
    }
}

} // namespace rock_hero::common::core
