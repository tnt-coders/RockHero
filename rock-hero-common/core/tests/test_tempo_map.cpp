#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

// Verifies the draft tempo map is a valid one-measure 4/4 grid at 120 BPM.
TEST_CASE("TempoMap default construction creates a usable grid", "[core][tempo-map]")
{
    const TempoMap tempo_map;

    REQUIRE(tempo_map.timeSignatures().size() == 1);
    REQUIRE(tempo_map.anchors().size() == 2);
    CHECK(tempo_map.timeSignatures().front() == TimeSignatureChange{});
    CHECK(tempo_map.anchors().front() == BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0});
    CHECK(tempo_map.anchors().back() == BeatAnchor{.measure = 2, .beat = 1, .seconds = 2.0});
    CHECK(tempo_map.secondsAtBeat(1, 3) == Catch::Approx(1.0));
}

// Verifies global beat indices account for carried-forward time-signature changes.
TEST_CASE("TempoMap indexes beats across meter changes", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            TimeSignatureChange{.measure = 3, .numerator = 3, .denominator = 4},
            TimeSignatureChange{.measure = 5, .numerator = 5, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            BeatAnchor{.measure = 6, .beat = 1, .seconds = 9.5},
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
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 10.0},
            BeatAnchor{.measure = 3, .beat = 1, .seconds = 14.0},
            BeatAnchor{.measure = 4, .beat = 1, .seconds = 15.5},
        },
    };

    CHECK(tempo_map.secondsAtBeat(2, 1) == Catch::Approx(12.0));
    CHECK(tempo_map.secondsAtNote(2, 2, Fraction{1, 2}) == Catch::Approx(12.75));
    CHECK(tempo_map.terminalGlobalBeatIndex() == 12);
}

// Verifies the shared global-beat interpolation query matches measure-address lookups and clamps
// outside the authored anchor range.
TEST_CASE("TempoMap resolves fractional global beat positions", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 10.0},
            BeatAnchor{.measure = 3, .beat = 1, .seconds = 14.0},
            BeatAnchor{.measure = 4, .beat = 1, .seconds = 15.5},
        },
    };

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
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 10.0},
            BeatAnchor{.measure = 3, .beat = 1, .seconds = 14.0},
            BeatAnchor{.measure = 4, .beat = 1, .seconds = 15.5},
        },
    };

    TempoMap::ForwardBeatTimeCursor cursor{tempo_map};
    for (const double position : {-1.0, 0.0, 2.5, 4.0, 8.0, 10.0, 12.0, 99.0})
    {
        CHECK(cursor.secondsAt(position) == tempo_map.secondsAtGlobalBeatPosition(position));
    }
}

// Verifies anchor interpolation stays aligned with the meter-aware beat axis across signature
// changes, exercising the derived segment and anchor index tables together.
TEST_CASE("TempoMap interpolates across meter changes", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            TimeSignatureChange{.measure = 3, .numerator = 3, .denominator = 4},
            TimeSignatureChange{.measure = 5, .numerator = 5, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            BeatAnchor{.measure = 6, .beat = 1, .seconds = 9.5},
        },
    };

    // 19 beats span the anchors (4 + 4 + 3 + 3 + 5), so each beat lasts 0.5 seconds.
    CHECK(tempo_map.terminalGlobalBeatIndex() == 19);
    CHECK(tempo_map.secondsAtBeat(3, 1) == Catch::Approx(4.0));
    CHECK(tempo_map.secondsAtNote(5, 1, Fraction{1, 2}) == Catch::Approx(7.25));
    CHECK(tempo_map.secondsAtGlobalBeatPosition(14.5) == Catch::Approx(7.25));
}

// Verifies quarter-note tempo is constant inside each anchor span, switches at anchors, and
// clamps outside the authored range.
TEST_CASE("TempoMap reports quarter-note tempo per anchor span", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 10.0},
            BeatAnchor{.measure = 3, .beat = 1, .seconds = 14.0},
            BeatAnchor{.measure = 4, .beat = 1, .seconds = 15.5},
        },
    };

    // First span: 8 beats over 4 seconds; second span: 4 beats over 1.5 seconds.
    CHECK(tempo_map.quarterNoteBpmAtSeconds(11.0) == Catch::Approx(120.0));
    CHECK(tempo_map.quarterNoteBpmAtSeconds(14.0) == Catch::Approx(160.0));
    CHECK(tempo_map.quarterNoteBpmAtSeconds(14.75) == Catch::Approx(160.0));
    CHECK(tempo_map.quarterNoteBpmAtSeconds(9.0) == Catch::Approx(120.0));
    CHECK(tempo_map.quarterNoteBpmAtSeconds(99.0) == Catch::Approx(160.0));
}

// Verifies the quarter-note reference scales by the signature denominator, so an x/8 meter with
// 120 eighth-note beats per minute reads as 60 quarter notes per minute.
TEST_CASE("TempoMap quarter-note tempo uses the active denominator", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 6, .denominator = 8},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            BeatAnchor{.measure = 2, .beat = 1, .seconds = 3.0},
        },
    };

    CHECK(tempo_map.quarterNoteBpmAtSeconds(1.0) == Catch::Approx(60.0));
}

// Verifies the seconds-to-beat inverse mirrors the forward query, including clamping outside the
// authored anchor range and crossing an un-anchored denominator change.
TEST_CASE("TempoMap resolves beat positions from seconds", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            TimeSignatureChange{.measure = 2, .numerator = 7, .denominator = 8},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            BeatAnchor{.measure = 3, .beat = 1, .seconds = 3.75},
        },
    };

    CHECK(tempo_map.beatPositionAtSeconds(0.5) == Catch::Approx(1.0));
    CHECK(tempo_map.beatPositionAtSeconds(2.25) == Catch::Approx(5.0));
    CHECK(tempo_map.beatPositionAtSeconds(-1.0) == Catch::Approx(0.0));
    CHECK(tempo_map.beatPositionAtSeconds(99.0) == Catch::Approx(11.0));
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
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            TimeSignatureChange{.measure = 2, .numerator = 7, .denominator = 8},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            BeatAnchor{.measure = 3, .beat = 1, .seconds = 3.75},
        },
    };

    CHECK(tempo_map.quarterNoteBpmAtSeconds(1.0) == Catch::Approx(120.0));
    CHECK(tempo_map.quarterNoteBpmAtSeconds(3.0) == Catch::Approx(120.0));
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
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            TimeSignatureChange{.measure = 3, .numerator = 3, .denominator = 4},
            TimeSignatureChange{.measure = 5, .numerator = 5, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            BeatAnchor{.measure = 6, .beat = 1, .seconds = 9.5},
        },
    };

    // 19 beats over 9.5 seconds puts measure 3 at 4.0s and measure 5 at 7.0s.
    CHECK(tempo_map.timeSignatureAtSeconds(3.9).numerator == 4);
    CHECK(tempo_map.timeSignatureAtSeconds(4.0).numerator == 3);
    CHECK(tempo_map.timeSignatureAtSeconds(7.2).numerator == 5);
    CHECK(tempo_map.timeSignatureAtSeconds(-1.0).numerator == 4);
    CHECK(tempo_map.timeSignatureAtSeconds(99.0).numerator == 5);
}

// Verifies the generated fallback map extends to a terminal downbeat after the audio.
TEST_CASE("TempoMap defaultMap covers audio duration", "[core][tempo-map]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{5.1});

    REQUIRE(tempo_map.anchors().size() == 2);
    CHECK(tempo_map.anchors().back() == BeatAnchor{.measure = 4, .beat = 1, .seconds = 6.0});
}

// Verifies beat positions below a first anchor that is not measure 1 beat 1 clamp to that
// anchor's time instead of extrapolating the first span backwards, so the forward map agrees
// with beatPositionAtSeconds' inverse clamp. Package validation forbids such maps, but the
// value constructor accepts them and the documented contract still applies.
TEST_CASE("TempoMap clamps beat positions below the first anchor", "[core][tempo-map]")
{
    const TempoMap tempo_map{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 2, .beat = 1, .seconds = 10.0},
            BeatAnchor{.measure = 3, .beat = 1, .seconds = 14.0},
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

} // namespace rock_hero::common::core
