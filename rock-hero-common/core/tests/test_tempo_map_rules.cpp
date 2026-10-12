#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

using rock_hero::common::core::testing::anchor;
using rock_hero::common::core::testing::commonTime;
using rock_hero::common::core::testing::commonTimeMap;
using rock_hero::common::core::testing::metersAt;

// A start anchor and a terminal downbeat two 120 BPM measures later.
[[nodiscard]] std::vector<BeatAnchor> twoMeasureAnchors()
{
    return {
        anchor("1:1", 0),
        anchor("3:1", 4000),
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

// The refusal a common-time map over the given anchors must produce.
[[nodiscard]] TempoMapErrorCode refusalOf(std::vector<BeatAnchor> anchors)
{
    return refusalOf(commonTime(), std::move(anchors));
}

} // namespace

// Verifies the maps the editor and the reader produce in practice pass every rule.
TEST_CASE("Tempo map rules accept well-formed maps", "[core][tempo-map]")
{
    CHECK(validateTempoMapRules(TempoMap::defaultMap(TimeDuration{93.5})).has_value());
    CHECK(validateTempoMapRules(commonTimeMap(twoMeasureAnchors())).has_value());
}

// Verifies an anchor may stand between two beats, and two may share one beat, in position order.
TEST_CASE("Tempo map rules accept sub-beat anchors", "[core][tempo-map]")
{
    CHECK(validateTempoMapRules(commonTimeMap({
                                    anchor("1:1", 0),
                                    anchor("1:3", 900),
                                    anchor("1:3+1/2", 1000),
                                    anchor("2:1+1/3", 2100),
                                    anchor("3:1", 4000),
                                }))
              .has_value());
}

// Verifies each structural rule refuses its own violation with its own code.
TEST_CASE("Tempo map rules refuse each violation", "[core][tempo-map]")
{
    CHECK(refusalOf({}, twoMeasureAnchors()) == TempoMapErrorCode::MissingTimeSignature);
    CHECK(
        refusalOf(
            {TimeSignatureChange{
                .place = Fraction{1}, .meter = {.numerator = 4, .denominator = 4}
            }},
            twoMeasureAnchors()) == TempoMapErrorCode::FirstTimeSignatureLate);
    CHECK(
        refusalOf(
            metersAt({{.at = "1:1", .numerator = 4, .denominator = 3}}), twoMeasureAnchors()) ==
        TempoMapErrorCode::InvalidTimeSignature);
    CHECK(
        refusalOf(
            metersAt({{.at = "1:1", .numerator = 4, .denominator = 512}}), twoMeasureAnchors()) ==
        TempoMapErrorCode::InvalidTimeSignature);
    CHECK(
        refusalOf(
            metersAt(
                {{.at = "1:1", .numerator = 4, .denominator = 4},
                 {.at = "2:1", .numerator = 3, .denominator = 4},
                 {.at = "2:1", .numerator = 5, .denominator = 4}}),
            twoMeasureAnchors()) == TempoMapErrorCode::InvalidTimeSignature);
    CHECK(refusalOf({anchor("1:1", 0)}) == TempoMapErrorCode::MissingAnchors);
    CHECK(refusalOf({anchor("1:2", 0), anchor("3:1", 4000)}) == TempoMapErrorCode::FirstAnchorLate);
    CHECK(refusalOf({anchor("1:1", 0), anchor("3:1", -1000)}) == TempoMapErrorCode::InvalidAnchor);
    CHECK(
        refusalOf({anchor("1:1", 0), anchor("2:1", 3000), anchor("3:1", 3000)}) ==
        TempoMapErrorCode::UnorderedAnchors);
}

// The terminal is an anchor like any other: a song may end mid-measure, on a short final bar, and
// the last reign keeps the governing meter rather than deriving one for the cut.
TEST_CASE("Tempo map rules accept a terminal mid-measure", "[core][tempo-map]")
{
    const TempoMap short_last_bar = commonTimeMap({anchor("1:1", 0), anchor("2:2+1/2", 4000)});
    CHECK(validateTempoMapRules(short_last_bar).has_value());
    CHECK(short_last_bar.reigns().size() == 1);
    CHECK_FALSE(short_last_bar.timeSignatureAt(2).derived);
}

// A meter may start mid-measure, cutting the bar before it short, but only where the cut bar is
// itself a meter: its place is a whole number of 1/256 notes (the meter lattice), and the reign it
// ends holds at least one whole measure. The refusal names both meters where the charter sees them.
TEST_CASE("Tempo map rules place a meter on the meter lattice", "[core][tempo-map]")
{
    const std::vector<BeatAnchor> anchors{anchor("1:1", 0), anchor("9:1", 16000)};
    CHECK(validateTempoMapRules(
              TempoMap{
                  metersAt(
                      {{.at = "1:1", .numerator = 4, .denominator = 4},
                       {.at = "6:2", .numerator = 7, .denominator = 8}}),
                  anchors
              })
              .has_value());
    CHECK(validateTempoMapRules(
              TempoMap{
                  {TimeSignatureChange{.place = {}, .meter = {.numerator = 4, .denominator = 4}},
                   TimeSignatureChange{
                       .place = Fraction{257, 256}, .meter = {.numerator = 7, .denominator = 8}
                   }},
                  anchors
              })
              .has_value());

    // A third of a beat, and a 512th note: no meter can state the bar they cut.
    for (const Fraction off_lattice : {Fraction{1, 12}, Fraction{1, 512}, Fraction{5, 3}})
    {
        CAPTURE(off_lattice.numerator, off_lattice.denominator);
        const auto result = validateTempoMapRules(
            TempoMap{
                {TimeSignatureChange{.place = {}, .meter = {.numerator = 4, .denominator = 4}},
                 TimeSignatureChange{
                     .place = off_lattice, .meter = {.numerator = 7, .denominator = 8}
                 }},
                anchors
            });
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == TempoMapErrorCode::InvalidTimeSignature);
        CHECK(result.error().message.find("1/256") != std::string::npos);
    }

    // The 9/8 at place 5 (6:1) holds no whole measure before a 3/4 at place 6, one whole note
    // later. The table derives the whole reign as one short bar, so the 3/4 reads as 7:1 — the two
    // chips at one downbeat the rule exists to refuse.
    const auto short_reign = validateTempoMapRules(
        TempoMap{
            {TimeSignatureChange{.place = {}, .meter = {.numerator = 4, .denominator = 4}},
             TimeSignatureChange{.place = Fraction{5}, .meter = {.numerator = 9, .denominator = 8}},
             TimeSignatureChange{
                 .place = Fraction{6}, .meter = {.numerator = 3, .denominator = 4}
             }},
            anchors
        });
    REQUIRE_FALSE(short_reign.has_value());
    CHECK(short_reign.error().code == TempoMapErrorCode::ReignTooShort);
    CHECK(
        short_reign.error().message ==
        "the 9/8 at 6:1 governs no whole measure before the 3/4 at 7:1");
    // Exactly one measure is enough: the 3/4 one 9/8 bar after the 9/8 lands.
    CHECK(validateTempoMapRules(
              TempoMap{
                  {TimeSignatureChange{.place = {}, .meter = {.numerator = 4, .denominator = 4}},
                   TimeSignatureChange{
                       .place = Fraction{5}, .meter = {.numerator = 9, .denominator = 8}
                   },
                   TimeSignatureChange{
                       .place = Fraction{49, 8}, .meter = {.numerator = 3, .denominator = 4}
                   }},
                  anchors
              })
              .has_value());
}

// Verifies the anchor rules hold for a position between two beats: the first anchor is exactly
// 1:1, every anchor a valid grid position, and the order the full position's.
TEST_CASE("Tempo map rules refuse sub-beat anchor violations", "[core][tempo-map]")
{
    CHECK(
        refusalOf({anchor("1:1+1/2", 0), anchor("3:1", 4000)}) ==
        TempoMapErrorCode::FirstAnchorLate);

    // A seventh of a beat is no whole number of ticks, so the position is off the lattice.
    CHECK(
        refusalOf({anchor("1:1", 0), anchor("1:2+1/7", 600), anchor("3:1", 4000)}) ==
        TempoMapErrorCode::InvalidAnchor);
    CHECK(
        refusalOf({anchor("1:1", 0), anchor("1:5", 2000), anchor("3:1", 4000)}) ==
        TempoMapErrorCode::InvalidAnchor);

    // An offset of a whole beat names the next beat twice; no token can spell it, so the shape is
    // stated by hand.
    CHECK(
        refusalOf({
            anchor("1:1", 0),
            BeatAnchor{
                .position = GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1}},
                .milliseconds = 1000,
            },
            anchor("3:1", 4000),
        }) == TempoMapErrorCode::InvalidAnchor);

    CHECK(
        refusalOf(
            {anchor("1:1", 0), anchor("1:3", 900), anchor("1:2+1/2", 1000), anchor("3:1", 4000)}) ==
        TempoMapErrorCode::UnorderedAnchors);
    CHECK(
        refusalOf(
            {anchor("1:1", 0),
             anchor("1:2+1/2", 1000),
             anchor("1:3", 1000),
             anchor("3:1", 4000)}) == TempoMapErrorCode::UnorderedAnchors);
}

// Verifies the one seconds-to-milliseconds rounding: nearest, with halves away from zero, and a
// time just below zero landing on zero rather than a negative millisecond.
TEST_CASE("Anchor times round to the nearest whole millisecond", "[core][tempo-map]")
{
    CHECK(nearestAnchorMilliseconds(1.2344) == std::int64_t{1234});
    CHECK(nearestAnchorMilliseconds(1.2345) == std::int64_t{1235});
    CHECK(nearestAnchorMilliseconds(1.2346) == std::int64_t{1235});
    CHECK(nearestAnchorMilliseconds(0.0) == std::int64_t{0});
    CHECK(nearestAnchorMilliseconds(-0.0004) == std::int64_t{0});
    CHECK(validateTempoMapRules(
              commonTimeMap({anchor("1:1", 0), anchor("3:1", nearestAnchorMilliseconds(4.0004))}))
              .has_value());
}

} // namespace rock_hero::common::core
