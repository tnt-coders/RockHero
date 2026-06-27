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

// Verifies the generated fallback map extends to a terminal downbeat after the audio.
TEST_CASE("TempoMap defaultMap covers audio duration", "[core][tempo-map]")
{
    const TempoMap tempo_map = TempoMap::defaultMap(TimeDuration{5.1});

    REQUIRE(tempo_map.anchors().size() == 2);
    CHECK(tempo_map.anchors().back() == BeatAnchor{.measure = 4, .beat = 1, .seconds = 6.0});
}

} // namespace rock_hero::common::core
