#include "timeline/tempo_scale.h"
#include "timeline/timeline_readdress.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>
#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/editor/core/testing/song_timeline_fixtures.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Chart;
using common::core::Fraction;
using common::core::SongTimeline;
using common::core::TempoMap;
using common::core::testing::anchor;
using common::core::testing::metersAt;
using testing::at;
using testing::chartOf;
using testing::checkTimesHeld;
using testing::Dice;
using testing::makeNote;
using testing::oneNoteTimeline;
using testing::randomTimeline;
using testing::requireValid;
using testing::sliceTimeline;

using Catch::Matchers::WithinAbs;

// Whether every anchor of a map stays on the tick lattice scaled by a factor, so the scaled map
// times content exactly and only the content rounds.
[[nodiscard]] bool anchorsScaleExactly(const TempoMap& tempo_map, Fraction factor)
{
    return std::ranges::all_of(placedAnchorsOf(tempo_map), [factor](const PlacedAnchor& stood) {
        return common::core::isOnTickLattice(stood.whole_note * factor);
    });
}

// Scales and requires the factor to land.
[[nodiscard]] SongTimeline scaled(const SongTimeline& timeline, const Fraction factor)
{
    const std::expected<SongTimeline, std::string> result = scaledTimeline(timeline, factor);
    INFO((result.has_value() ? std::string{} : result.error()));
    REQUIRE(result.has_value());
    return *result;
}

} // namespace

// x2 is exact, so it always lands, passes every rule, holds every time and /2 restores the song
// token for token, the terminal included. /2 and 3/2 round content onto the lattice and never
// carry a meter off the meter lattice; 4/3 does wherever a meter's place is not a multiple of
// three 1/256 notes, and is refused for it. Where a factor lands and the map's own anchors scale
// exactly, every time holds within half a tick.
TEST_CASE("Scale Tempo holds every time over random songs", "[core][scale-tempo]")
{
    int round_trips = 0;
    int rounded_landed = 0;
    int lattice_refused = 0;
    int rule_refused = 0;
    int rounded_timed = 0;
    for (std::uint32_t seed = 1; seed <= 150; ++seed)
    {
        INFO("seed " << seed);
        Dice dice{seed};
        const SongTimeline before = randomTimeline(dice);
        requireValid(before);

        const SongTimeline twice = scaled(before, Fraction{2});
        requireValid(twice);
        checkTimesHeld(before, twice, 0.0);
        CHECK(scaled(twice, Fraction{1, 2}) == before);
        ++round_trips;

        for (const Fraction& factor : {Fraction{1, 2}, Fraction{3, 2}, Fraction{4, 3}})
        {
            const std::expected<SongTimeline, std::string> result = scaledTimeline(before, factor);
            if (!result.has_value())
            {
                CHECK(factor == Fraction{4, 3});
                CHECK(result.error().find("1/256") != std::string::npos);
                ++lattice_refused;
                continue;
            }
            if (!common::core::validateSongTimelineRules(*result).has_value())
            {
                // A reign halved below one of its measures, or two places rounded onto one tick:
                // the verb's rules refuse the result.
                ++rule_refused;
                continue;
            }
            ++rounded_landed;
            if (anchorsScaleExactly(before.tempo_map, factor))
            {
                checkTimesHeld(before, *result, 0.0);
                ++rounded_timed;
            }
        }
    }
    // The dice must reach every branch, or the property proves less than it claims.
    CHECK(round_trips == 150);
    CHECK(rounded_landed + lattice_refused + rule_refused == 450);
    CHECK(lattice_refused > 0);
    CHECK(rule_refused > 0);
    CHECK(rounded_timed > 0);
}

// The slice fixture's 3/4 at measure 3 lands on measure 5 under x2: its reign before it, two
// measures of 4/4, becomes four. The terminal at 6:1 (4 1/4 whole notes) lands on 8 1/2, which is
// 11:1, keeping its 8500 ms, and the Bridge at 4:1 lands on 7:1.
TEST_CASE("Scale Tempo by 2 lands a meter at measure m on measure 2m-1", "[core][scale-tempo]")
{
    const SongTimeline before = sliceTimeline();
    requireValid(before);
    const SongTimeline after = scaled(before, Fraction{2});
    requireValid(after);
    CHECK(
        after.tempo_map.timeSignatures() == metersAt(
                                                {{.at = "1:1", .numerator = 4, .denominator = 4},
                                                 {.at = "5:1", .numerator = 3, .denominator = 4}}));
    CHECK(after.tempo_map.anchors().back() == anchor("11:1", 8500));
    REQUIRE(after.sections.size() == 2);
    CHECK(after.sections[1].position == at("7:1"));
    checkTimesHeld(before, after, 0.0);
}

// Three measures of 4/4 halve to one and a half, so the 6/8 after them keeps its place at whole
// note 1 1/2: measure 2 is a derived 2/4 bar and the 6/8 begins on 3:1. The terminal, at 4 1/2
// whole notes before, lands on 2 1/4, one 6/8 bar past the change: 4:1 at its 9000 ms.
TEST_CASE("Scale Tempo by 1/2 cuts a reign short before a later meter", "[core][scale-tempo]")
{
    const SongTimeline before{
        .tempo_map =
            TempoMap{
                metersAt(
                    {{.at = "1:1", .numerator = 4, .denominator = 4},
                     {.at = "4:1", .numerator = 6, .denominator = 8}}),
                {anchor("1:1", 0), anchor("6:1", 9000)},
            },
        .sections = {},
        .arrangements = {},
    };
    requireValid(before);
    const SongTimeline after = scaled(before, Fraction{1, 2});
    requireValid(after);
    REQUIRE(after.tempo_map.timeSignatures().size() == 2);
    CHECK(after.tempo_map.timeSignatures()[1].place == Fraction{3, 2});
    CHECK(
        after.tempo_map.timeSignatureAt(2) == common::core::MeterReign{
                                                  .first_measure = 2,
                                                  .place = Fraction{1},
                                                  .meter = {.numerator = 2, .denominator = 4},
                                                  .derived = true,
                                              });
    CHECK(after.tempo_map.timeSignatureAt(3).first_measure == 3);
    CHECK(after.tempo_map.anchors().back() == anchor("4:1", 9000));
}

// A factor whose reduced denominator is not a power of two can carry a meter off the meter
// lattice: the 6/8 at whole note 4 would land on 16/3, no whole number of 1/256 notes, so the
// factor is refused naming the meter where it stands now. The same factor on a song whose only
// meter is at the start lands, every time held and the content rounded onto the tick lattice.
TEST_CASE(
    "Scale Tempo refuses a factor carrying a meter off the meter lattice", "[core][scale-tempo]")
{
    const SongTimeline before{
        .tempo_map =
            TempoMap{
                metersAt(
                    {{.at = "1:1", .numerator = 4, .denominator = 4},
                     {.at = "5:1", .numerator = 6, .denominator = 8}}),
                {anchor("1:1", 0), anchor("7:1", 11000)},
            },
        .sections = {},
        .arrangements = {},
    };
    requireValid(before);
    const std::expected<SongTimeline, std::string> after = scaledTimeline(before, Fraction{4, 3});
    REQUIRE_FALSE(after.has_value());
    CHECK(
        after.error() ==
        "the 6/8 time signature at 5:1 would not start a whole number of 1/256 notes from the "
        "song's start");
}

TEST_CASE("Scale Tempo by 4/3 lands on a single-meter song", "[core][scale-tempo]")
{
    // Three whole notes in 6 s become four; the note at whole note 1 (2 s) lands on 4/3, which is
    // 2:2+1/3 in 4/4, still at 2 s, its quarter-note ring a third of a whole note.
    const SongTimeline before = oneNoteTimeline(
        common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("4:1", 6000)}),
        makeNote(at("2:1"), 1, 5, Fraction{1, 4}, {}));
    requireValid(before);
    const SongTimeline after = scaled(before, Fraction{4, 3});
    requireValid(after);
    CHECK(
        after.tempo_map ==
        common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("5:1", 6000)}));
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes[0].position == at("2:2+1/3"));
    CHECK(chart.notes[0].sustain == Fraction{1, 3});
    CHECK_THAT(after.tempo_map.secondsAt(chart.notes[0].position), WithinAbs(2.0, 1e-9));
    checkTimesHeld(before, after, 0.0);
}

// With no later meter nothing refuses and nothing is re-timed: three measures of 4/4 halve to one
// and a half, so the terminal stands on 2:3 at its 6000 ms and the song ends on a half bar. The
// note at 3:1 keeps its 4 s on 2:1, its quarter-note ring halved to an eighth.
TEST_CASE(
    "Scale Tempo by 1/2 with no later meter leaves the terminal mid-measure", "[core][scale-tempo]")
{
    const SongTimeline before = oneNoteTimeline(
        common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("4:1", 6000)}),
        makeNote(at("3:1"), 1, 5, Fraction{1, 4}, {}));
    requireValid(before);
    const SongTimeline after = scaled(before, Fraction{1, 2});
    requireValid(after);
    CHECK(
        after.tempo_map ==
        common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("2:3", 6000)}));
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes[0].position == at("2:1"));
    CHECK(chart.notes[0].sustain == Fraction{1, 8});
    CHECK_THAT(after.tempo_map.secondsAt(chart.notes[0].position), WithinAbs(4.0, 1e-9));
}

// A factor is a positive whole number or a ratio of two, reduced; anything else reads as nothing.
TEST_CASE("Scale factor text reads positive ratios only", "[core][scale-tempo]")
{
    CHECK(parseScaleFactor("2") == Fraction{2});
    CHECK(parseScaleFactor("1/2") == Fraction{1, 2});
    CHECK(parseScaleFactor("3/2") == Fraction{3, 2});
    CHECK(parseScaleFactor(" 3 / 2 ") == Fraction{3, 2});
    CHECK(parseScaleFactor("4/2") == Fraction{2});
    for (const char* const refused :
         {"", " ", "0", "0/1", "1/0", "-2", "+2", "1.5", "x2", "2/", "/2", "1/2/3", "2 2"})
    {
        INFO(refused);
        const std::expected<Fraction, std::string> factor = parseScaleFactor(refused);
        REQUIRE_FALSE(factor.has_value());
        CHECK(factor.error() == std::string{"not a scale factor: "} + refused);
    }
}

// Each term is bounded after reduction: 16, 1/16, 16/15 and 32/2 (16) pass, while 17, 1/17, 34/2
// (17) and 17/16 are refused, naming the limit.
TEST_CASE("Scale factor terms are bounded after reduction", "[core][scale-tempo]")
{
    CHECK(parseScaleFactor("16") == Fraction{16});
    CHECK(parseScaleFactor("1/16") == Fraction{1, 16});
    CHECK(parseScaleFactor("16/15") == Fraction{16, 15});
    CHECK(parseScaleFactor("32/2") == Fraction{16});
    for (const char* const refused : {"17", "1/17", "34/2", "17/16"})
    {
        INFO(refused);
        const std::expected<Fraction, std::string> factor = parseScaleFactor(refused);
        REQUIRE_FALSE(factor.has_value());
        CHECK(
            factor.error() ==
            std::string{"a scale factor's terms must each be at most 16: "} + refused);
    }
}

} // namespace rock_hero::editor::core
