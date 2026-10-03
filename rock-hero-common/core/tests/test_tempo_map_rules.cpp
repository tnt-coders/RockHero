#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// The 4/4 meter every case below starts from.
[[nodiscard]] std::vector<TimeSignatureChange> commonTime()
{
    return {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}};
}

// A start anchor and a terminal downbeat two 120 BPM measures later.
[[nodiscard]] std::vector<BeatAnchor> twoMeasureAnchors()
{
    return {
        BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
        BeatAnchor{.measure = 3, .beat = 1, .seconds = 4.0},
    };
}

// Validates a map built from the given parts and returns the refusal code it must produce.
[[nodiscard]] TempoMapErrorCode refusalOf(
    std::vector<TimeSignatureChange> signatures, std::vector<BeatAnchor> anchors)
{
    const auto result = validateTempoMapRules(TempoMap{std::move(signatures), std::move(anchors)});
    REQUIRE_FALSE(result.has_value());
    return result.error().code;
}

} // namespace

// Verifies the maps the editor and the reader produce in practice pass every rule.
TEST_CASE("Tempo map rules accept well-formed maps", "[core][tempo-map]")
{
    CHECK(validateTempoMapRules(TempoMap::defaultMap(TimeDuration{93.5})).has_value());
    CHECK(validateTempoMapRules(TempoMap{commonTime(), twoMeasureAnchors()}).has_value());
}

// Verifies each structural rule refuses its own violation with its own code.
TEST_CASE("Tempo map rules refuse each violation", "[core][tempo-map]")
{
    CHECK(refusalOf({}, twoMeasureAnchors()) == TempoMapErrorCode::MissingTimeSignature);
    CHECK(
        refusalOf(
            {TimeSignatureChange{.measure = 2, .numerator = 4, .denominator = 4}},
            twoMeasureAnchors()) == TempoMapErrorCode::FirstTimeSignatureLate);
    CHECK(
        refusalOf(
            {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 3}},
            twoMeasureAnchors()) == TempoMapErrorCode::InvalidTimeSignature);
    CHECK(
        refusalOf(
            {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 512}},
            twoMeasureAnchors()) == TempoMapErrorCode::InvalidTimeSignature);
    CHECK(
        refusalOf(commonTime(), {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0}}) ==
        TempoMapErrorCode::MissingAnchors);
    CHECK(
        refusalOf(
            commonTime(),
            {BeatAnchor{.measure = 1, .beat = 2, .seconds = 0.0},
             BeatAnchor{.measure = 3, .beat = 1, .seconds = 4.0}}) ==
        TempoMapErrorCode::FirstAnchorLate);
    CHECK(
        refusalOf(
            commonTime(),
            {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
             BeatAnchor{.measure = 3, .beat = 1, .seconds = -1.0}}) ==
        TempoMapErrorCode::InvalidAnchor);
    CHECK(
        refusalOf(
            commonTime(),
            {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
             BeatAnchor{.measure = 3, .beat = 1, .seconds = 4.0004}}) ==
        TempoMapErrorCode::AnchorOffSecondsGrid);
    CHECK(
        refusalOf(
            commonTime(),
            {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
             BeatAnchor{.measure = 1, .beat = 5, .seconds = 2.0},
             BeatAnchor{.measure = 3, .beat = 1, .seconds = 4.0}}) ==
        TempoMapErrorCode::AnchorBeatOutsideMeasure);
    CHECK(
        refusalOf(
            commonTime(),
            {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
             BeatAnchor{.measure = 2, .beat = 1, .seconds = 3.0},
             BeatAnchor{.measure = 3, .beat = 1, .seconds = 3.0}}) ==
        TempoMapErrorCode::UnorderedAnchors);
    CHECK(
        refusalOf(
            commonTime(),
            {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
             BeatAnchor{.measure = 2, .beat = 2, .seconds = 4.0}}) ==
        TempoMapErrorCode::TerminalOffDownbeat);
}

// Verifies rounding lands on the millisecond grid the rules accept, half a millisecond deciding.
TEST_CASE("Anchor seconds round to the millisecond grid", "[core][tempo-map]")
{
    CHECK_THAT(nearestAnchorSeconds(1.2344), Catch::Matchers::WithinAbs(1.234, 1.0e-12));
    CHECK_THAT(nearestAnchorSeconds(1.2346), Catch::Matchers::WithinAbs(1.235, 1.0e-12));
    CHECK_THAT(nearestAnchorSeconds(0.0), Catch::Matchers::WithinAbs(0.0, 1.0e-12));

    const TempoMap rounded{
        commonTime(),
        {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
         BeatAnchor{.measure = 3, .beat = 1, .seconds = nearestAnchorSeconds(4.0004)}}
    };
    CHECK(validateTempoMapRules(rounded).has_value());
}

} // namespace rock_hero::common::core
