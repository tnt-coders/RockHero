#include "timeline/tempo_scale.h"
#include "timeline/timeline_readdress.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>
#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
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
using common::core::GridPosition;
using common::core::SongTimeline;
using common::core::TempoMap;
using common::core::TimeSignatureChange;
using common::core::testing::anchor;
using testing::at;
using testing::chartOf;
using testing::Dice;
using testing::forEachSlicePair;
using testing::g_reseated_time_tolerance_seconds;
using testing::g_time_tolerance_seconds;
using testing::makeNote;
using testing::oneNoteTimeline;
using testing::randomTimeline;
using testing::requireValid;
using testing::SliceKind;
using testing::SlicePair;
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

// Half the longer of the two ticks beside a position: the most a rounding onto the lattice moves it
// in time.
[[nodiscard]] double halfTickSeconds(const TempoMap& tempo_map, const GridPosition& position)
{
    const Fraction tick = common::core::g_tick_quantum_note_value;
    const double here = tempo_map.secondsAt(position);
    const double later =
        tempo_map.secondsAt(
            common::core::advanceGridPositionByWholeNotes(tempo_map, position, tick)) -
        here;
    const double earlier =
        here - tempo_map.secondsAt(
                   common::core::advanceGridPositionByWholeNotes(
                       tempo_map, position, Fraction{-tick.numerator, tick.denominator}));
    return std::max(later, earlier) / 2.0;
}

// Checks every content position of a scaled timeline against the one it came from: its time held
// within half a tick of the new grid, the most the rounding onto the lattice moves it, and within
// the re-seat's half millisecond more where the terminal re-seated, re-timing the last span.
// Sections are navigation and may snap, so they are the section rules' to judge.
void checkScaledTimes(const SongTimeline& before, const SongTimeline& after)
{
    const TempoMap& old_map = before.tempo_map;
    const TempoMap& new_map = after.tempo_map;
    const bool reseated =
        new_map.anchors().back().milliseconds != old_map.anchors().back().milliseconds;
    const double reseat_slack = reseated ? g_reseated_time_tolerance_seconds : 0.0;
    forEachSlicePair(before, after, [&old_map, &new_map, reseat_slack](const SlicePair& pair) {
        if (pair.kind == SliceKind::Section)
        {
            return;
        }
        CHECK_THAT(
            new_map.secondsAt(pair.new_position),
            WithinAbs(
                old_map.secondsAt(pair.old_position),
                halfTickSeconds(new_map, pair.new_position) + reseat_slack +
                    g_time_tolerance_seconds));
    });
}

} // namespace

// x2 is exact, so it always lands, passes every rule, holds every time and /2 restores the song
// token for token. /2 and 3/2 round content onto the lattice: where they land and the map's own
// anchors scale exactly, every time holds within half a tick.
TEST_CASE("Scale Tempo holds every time over random songs", "[core][scale-tempo]")
{
    int round_trips = 0;
    int rounded_landed = 0;
    int rounded_refused = 0;
    int rounded_colliding = 0;
    int rounded_timed = 0;
    for (std::uint32_t seed = 1; seed <= 150; ++seed)
    {
        INFO("seed " << seed);
        Dice dice{seed};
        const SongTimeline before = randomTimeline(dice);
        requireValid(before);

        const std::expected<SongTimeline, ReaddressError> doubled =
            scaledTimeline(before, Fraction{2});
        REQUIRE(doubled.has_value());
        const SongTimeline& twice = *doubled;
        requireValid(twice);
        checkScaledTimes(before, twice);
        const std::expected<SongTimeline, ReaddressError> halved =
            scaledTimeline(twice, Fraction{1, 2});
        REQUIRE(halved.has_value());
        CHECK(*halved == before);
        ++round_trips;

        for (const Fraction& factor : {Fraction{1, 2}, Fraction{3, 2}})
        {
            const std::expected<SongTimeline, ReaddressError> scaled =
                scaledTimeline(before, factor);
            if (!scaled.has_value())
            {
                CHECK(scaled.error().code == ReaddressErrorCode::MeterOffDownbeat);
                ++rounded_refused;
                continue;
            }
            if (!common::core::validateSongTimelineRules(*scaled).has_value())
            {
                // Two places rounded onto one tick: the verb's rules refuse the result.
                ++rounded_colliding;
                continue;
            }
            ++rounded_landed;
            if (anchorsScaleExactly(before.tempo_map, factor))
            {
                checkScaledTimes(before, *scaled);
                ++rounded_timed;
            }
        }
    }
    // The dice must reach every branch, or the property proves less than it claims.
    CHECK(round_trips == 150);
    CHECK(rounded_landed + rounded_refused + rounded_colliding == 300);
    CHECK(rounded_refused > 0);
    CHECK(rounded_timed > 0);
}

// The slice fixture's 3/4 at measure 3 lands on measure 5 under x2: its reign before it, two
// measures of 4/4, becomes four. The terminal at 6:1 (4 1/4 whole notes) lands on 8 1/2, which is
// 11:1, keeping its 8500 ms, and the Bridge at 4:1 lands on 7:1.
TEST_CASE("Scale Tempo by 2 lands a meter at measure m on measure 2m-1", "[core][scale-tempo]")
{
    const SongTimeline before = sliceTimeline();
    requireValid(before);
    const std::expected<SongTimeline, ReaddressError> after = scaledTimeline(before, Fraction{2});
    REQUIRE(after.has_value());
    const SongTimeline& scaled = *after;
    requireValid(scaled);
    CHECK(
        scaled.tempo_map.timeSignatures() ==
        std::vector{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 5, .meter = {.numerator = 3, .denominator = 4}},
        });
    CHECK(scaled.tempo_map.anchors().back() == anchor("11:1", 8500));
    REQUIRE(scaled.sections.size() == 2);
    CHECK(scaled.sections[1].position == at("7:1"));
    checkScaledTimes(before, scaled);
}

// Three measures of 4/4 halve to one and a half, so the 6/8 after them would start mid-measure:
// the factor is refused, naming the meter where it stands now.
TEST_CASE("Scale Tempo refuses an odd reign before a later meter", "[core][scale-tempo]")
{
    const SongTimeline before{
        .tempo_map =
            TempoMap{
                {
                    TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
                    TimeSignatureChange{.measure = 4, .meter = {.numerator = 6, .denominator = 8}},
                },
                {anchor("1:1", 0), anchor("6:1", 9000)},
            },
        .sections = {},
        .arrangements = {},
    };
    requireValid(before);
    const std::expected<SongTimeline, ReaddressError> after =
        scaledTimeline(before, Fraction{1, 2});
    REQUIRE_FALSE(after.has_value());
    CHECK(after.error().code == ReaddressErrorCode::MeterOffDownbeat);
    CHECK(
        after.error().message ==
        "the 6/8 time signature at 4:1 would start mid-measure under the 4/4 before it");
}

// With no later meter nothing refuses: three measures of 4/4 halve to one and a half, so the
// terminal lands on 2:3 and re-seats on 3:1, timed at the halved span's 1000 ms quarter. The note
// at 3:1 keeps its 4 s on 2:1, its one-beat ring halved to half a beat.
TEST_CASE("Scale Tempo by 1/2 with no later meter re-seats the terminal", "[core][scale-tempo]")
{
    const SongTimeline before = oneNoteTimeline(
        common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("4:1", 6000)}),
        makeNote(at("3:1"), 1, 5, Fraction{1}, {}));
    requireValid(before);
    const std::expected<SongTimeline, ReaddressError> after =
        scaledTimeline(before, Fraction{1, 2});
    REQUIRE(after.has_value());
    const SongTimeline& scaled = *after;
    requireValid(scaled);
    CHECK(
        scaled.tempo_map ==
        common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("3:1", 8000)}));
    const Chart& chart = chartOf(scaled, 0);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes[0].position == at("2:1"));
    CHECK(chart.notes[0].sustain == Fraction{1, 2});
    CHECK_THAT(scaled.tempo_map.secondsAt(chart.notes[0].position), WithinAbs(4.0, 1e-9));
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
