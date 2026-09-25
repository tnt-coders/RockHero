#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/highway/highway_window.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// The pitched slide ease at half progress, the curve a glide-locked or ordinary window transition
// uses.
[[nodiscard]] double halfProgressWeight()
{
    return 0.5;
}

// The unpitched release ease at half progress (1 - sin(pi/4)), the curve a slide-out's window
// transition uses instead.
[[nodiscard]] double unpitchedHalfProgressWeight()
{
    return 1.0 - std::sin(std::numbers::pi / 4.0);
}

// Two arrivals: an instant arrival at fret 3 width 4 (lines 2-6), then a ramped move to a wider
// fret-8 width-6 window (lines 7-13) whose two-second ramp starts at 4.0.
[[nodiscard]] std::vector<HighwayHandArrival> makePlacements()
{
    return {
        HighwayHandArrival{
            .seconds = 2.0,
            .low_line = 2.0,
            .high_line = 6.0,
            .ramp_seconds = 0.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        },
        HighwayHandArrival{
            .seconds = 6.0,
            .low_line = 7.0,
            .high_line = 13.0,
            .ramp_seconds = 2.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        },
    };
}

// The same move with the arriving placement's ramp marked unpitched, so the two ease families can
// be compared over identical geometry rather than against a second hand-written fixture.
[[nodiscard]] std::vector<HighwayHandArrival> makeUnpitchedPlacements()
{
    std::vector<HighwayHandArrival> placements = makePlacements();
    placements.back().unpitched_ramp = true;
    return placements;
}

// A settled window at lines 2-6, then an unpitched one-second approach to lines 7-13 arriving at
// 6.0 whose final `settle_seconds` settle into the arrival.
[[nodiscard]] std::vector<HighwayHandArrival> makeSettlingPlacements(const double settle_seconds)
{
    return {
        HighwayHandArrival{
            .seconds = 2.0,
            .low_line = 2.0,
            .high_line = 6.0,
            .ramp_seconds = 0.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        },
        HighwayHandArrival{
            .seconds = 6.0,
            .low_line = 7.0,
            .high_line = 13.0,
            .ramp_seconds = 1.0,
            .unpitched_ramp = true,
            .settle_seconds = settle_seconds,
        },
    };
}

} // namespace

// Outside every ramp the window is a step function of the arrivals: the first placement's
// window already holds before its arrival (the opening scroll shows where the hand belongs), each
// settled window from its (inclusive) arrival on, and a zero ramp steps exactly at its arrival
// instant. The nut window applies only to chartless boards.
TEST_CASE("Hand window holds settled extents outside ramps", "[core][highway][window]")
{
    const std::vector<HighwayHandArrival> placements = makePlacements();

    CHECK(highwayHandWindowAt({}, 5.0) == HighwayHandWindow{.low_line = 0.0, .high_line = 4.0});
    CHECK(
        highwayHandWindowAt(placements, 1.9) ==
        HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
    CHECK(
        highwayHandWindowAt(placements, 2.0) ==
        HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
    CHECK(
        highwayHandWindowAt(placements, 3.9) ==
        HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
    CHECK(
        highwayHandWindowAt(placements, 6.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});
    CHECK(
        highwayHandWindowAt(placements, 9.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});
}

// Inside a ramp both edges ease independently from the previous settled window toward the
// arriving one with the pitched slide curve, so a position move and a width morph are one
// mechanism and the border leaves and rejoins the settled edges tangentially.
TEST_CASE("Hand window eases both edges through a ramp", "[core][highway][window]")
{
    const std::vector<HighwayHandArrival> placements = makePlacements();

    // Ramp start is exact: at 4.0 the window has not yet moved.
    const HighwayHandWindow at_start = highwayHandWindowAt(placements, 4.0);
    CHECK(at_start.low_line == Catch::Approx(2.0));
    CHECK(at_start.high_line == Catch::Approx(6.0));

    // Half progress uses the pitched ease weight on each edge's own travel.
    const double weight = halfProgressWeight();
    const HighwayHandWindow mid = highwayHandWindowAt(placements, 5.0);
    CHECK(mid.low_line == Catch::Approx(2.0 + (5.0 * weight)));
    CHECK(mid.high_line == Catch::Approx(6.0 + (7.0 * weight)));

    // The first placement never sweeps in from the nut window: its own window pre-holds, so a
    // ramp on the first placement degenerates to no motion.
    const std::vector<HighwayHandArrival> opening{
        HighwayHandArrival{
            .seconds = 1.0,
            .low_line = 4.0,
            .high_line = 8.0,
            .ramp_seconds = 1.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        },
    };
    const HighwayHandWindow opening_mid = highwayHandWindowAt(opening, 0.5);
    CHECK(opening_mid.low_line == Catch::Approx(4.0));
    CHECK(opening_mid.high_line == Catch::Approx(8.0));
}

// A placement arriving on an unpitched slide-out eases with the slide-out curve, not the pitched
// one: same settled windows at both ends of the ramp, a different path between them. That
// difference is the flag's entire purpose, and the projection setting it is not evidence the
// window reads it.
TEST_CASE("Hand window eases an unpitched ramp with the slide-out curve", "[core][highway][window]")
{
    const std::vector<HighwayHandArrival> placements = makeUnpitchedPlacements();

    // Ramp start: the slide-out curve is zero at zero progress, so the previous window still holds.
    const HighwayHandWindow at_start = highwayHandWindowAt(placements, 4.0);
    CHECK(at_start.low_line == Catch::Approx(2.0));
    CHECK(at_start.high_line == Catch::Approx(6.0));

    // Arrival: full travel, so the arriving placement's own settled extent.
    CHECK(
        highwayHandWindowAt(placements, 6.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});

    // Half progress rides 1 - sin(pi/4) of each edge's own travel.
    const double weight = unpitchedHalfProgressWeight();
    const HighwayHandWindow mid = highwayHandWindowAt(placements, 5.0);
    CHECK(mid.low_line == Catch::Approx(2.0 + (5.0 * weight)));
    CHECK(mid.high_line == Catch::Approx(6.0 + (7.0 * weight)));

    // And it is genuinely the other family rather than the pitched curve relabeled: over the
    // identical move, the slide-out has given up less of its travel by half progress.
    const HighwayHandWindow pitched_mid = highwayHandWindowAt(makePlacements(), 5.0);
    CHECK(mid.low_line < pitched_mid.low_line);
    CHECK(mid.high_line < pitched_mid.high_line);
}

// The crop zone: over an unpitched ramp's final `settle_seconds` the window leaves the slide-out
// curve with its value and slope and comes to rest at the arrival with zero slope, where the
// curve alone would stop with slope. Before the zone it is the curve, unchanged.
TEST_CASE("Hand window settles an unpitched ramp over its crop zone", "[core][highway][window]")
{
    // Ramp [5.0, 6.0], settle over its final 0.15 s: the knee sits at progress 0.85 (5.85 s).
    const std::vector<HighwayHandArrival> settling = makeSettlingPlacements(0.15);
    const auto expected_on_curve = [](const double curve_progress) {
        const double weight = highwaySlideEaseWeight(curve_progress, true);
        return HighwayHandWindow{
            .low_line = 2.0 + (5.0 * weight),
            .high_line = 6.0 + (7.0 * weight),
        };
    };

    // (a) Before the settle begins the window rides the plain unpitched curve.
    for (const double progress : {0.25, 0.5, 0.85})
    {
        const HighwayHandWindow window = highwayHandWindowAt(settling, 5.0 + progress);
        const HighwayHandWindow expected = expected_on_curve(progress);
        CHECK(window.low_line == Catch::Approx(expected.low_line));
        CHECK(window.high_line == Catch::Approx(expected.high_line));
    }

    // (b) At the arrival the window is the settled target.
    CHECK(
        highwayHandWindowAt(settling, 6.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});

    // (c) Just before the arrival the window has all but come to rest: its gap to the target is a
    // small fraction of the gap the curve alone still has there, because it arrives with zero
    // slope where the curve arrives with slope.
    constexpr double just_before = 6.0 - 1.0e-3;
    const HighwayHandWindow near_arrival = highwayHandWindowAt(settling, just_before);
    const HighwayHandWindow curve_near_arrival = expected_on_curve(just_before - 5.0);
    CHECK(near_arrival.low_line == Catch::Approx(7.0).margin(1.0e-3));
    CHECK(near_arrival.high_line == Catch::Approx(13.0).margin(1.0e-3));
    CHECK(7.0 - near_arrival.low_line < (7.0 - curve_near_arrival.low_line) / 10.0);
    CHECK(13.0 - near_arrival.high_line < (13.0 - curve_near_arrival.high_line) / 10.0);

    // (d) The settle joins the curve without a jump at the knee.
    constexpr double knee = 5.85;
    const HighwayHandWindow before_knee = highwayHandWindowAt(settling, knee - 1.0e-6);
    const HighwayHandWindow after_knee = highwayHandWindowAt(settling, knee + 1.0e-6);
    CHECK(std::abs(after_knee.low_line - before_knee.low_line) < 1.0e-3);
    CHECK(std::abs(after_knee.high_line - before_knee.high_line) < 1.0e-3);

    // (e) A zero settle is the plain curve all the way to the arrival.
    const std::vector<HighwayHandArrival> plain = makeSettlingPlacements(0.0);
    for (const double progress : {0.25, 0.9, 0.999})
    {
        const HighwayHandWindow window = highwayHandWindowAt(plain, 5.0 + progress);
        const HighwayHandWindow expected = expected_on_curve(progress);
        CHECK(window.low_line == Catch::Approx(expected.low_line));
        CHECK(window.high_line == Catch::Approx(expected.high_line));
    }
}

// A settle longer than its ramp is clamped to the ramp: the whole approach becomes the settle, so
// the window still leaves the previous settled extent without a jump at the ramp's start and still
// arrives exactly at the arrival.
TEST_CASE("Hand window clamps a settle longer than its ramp", "[core][highway][window]")
{
    // Ramp [5.0, 6.0] with a settle of 3 s.
    const std::vector<HighwayHandArrival> overlong = makeSettlingPlacements(3.0);
    const HighwayHandWindow previous{.low_line = 2.0, .high_line = 6.0};

    // No jump at the ramp's start: the window there is the previous settled one, and an instant
    // later it has barely moved.
    CHECK(highwayHandWindowAt(overlong, 5.0) == previous);
    const HighwayHandWindow just_after = highwayHandWindowAt(overlong, 5.0 + 1.0e-6);
    CHECK(std::abs(just_after.low_line - previous.low_line) < 1.0e-3);
    CHECK(std::abs(just_after.high_line - previous.high_line) < 1.0e-3);

    // Exactly at the arrival: the settled target.
    CHECK(
        highwayHandWindowAt(overlong, 6.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});

    // Through the ramp it is the settle as long as the ramp itself.
    const std::vector<HighwayHandArrival> full = makeSettlingPlacements(1.0);
    for (const double progress : {0.1, 0.5, 0.9})
    {
        const HighwayHandWindow clamped = highwayHandWindowAt(overlong, 5.0 + progress);
        const HighwayHandWindow expected = highwayHandWindowAt(full, 5.0 + progress);
        CHECK(clamped.low_line == Catch::Approx(expected.low_line));
        CHECK(clamped.high_line == Catch::Approx(expected.high_line));
    }
}

// Coverage is the shared hit-line signal: full one lane inside either edge, zero one lane
// outside, ramping linearly across each moving edge so brightness crossfades and number fades
// track the sweeping border exactly.
TEST_CASE("Hand window line coverage ramps across the edges", "[core][highway][window]")
{
    const HighwayHandWindow settled{.low_line = 2.0, .high_line = 6.0};
    CHECK(highwayHandWindowLineCoverage(settled, 2.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(settled, 6.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(settled, 1.0) == Catch::Approx(0.0));
    CHECK(highwayHandWindowLineCoverage(settled, 7.0) == Catch::Approx(0.0));

    // Mid-sweep fractional edges: the line being exited fades, interior lines stay saturated.
    const HighwayHandWindow sweeping{.low_line = 2.5, .high_line = 6.5};
    CHECK(highwayHandWindowLineCoverage(sweeping, 2.0) == Catch::Approx(0.5));
    CHECK(highwayHandWindowLineCoverage(sweeping, 3.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(sweeping, 6.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(sweeping, 7.0) == Catch::Approx(0.5));
}

// THE LEG LOOKUP at its breakpoints: a leg is in progress from its ramp's start inclusive to its
// arrival exclusive, the first arrival never has one, and neither does a zero ramp.
TEST_CASE("Hand leg lookup finds the ramp in progress", "[core][highway][window]")
{
    std::vector<HighwayHandArrival> track = makePlacements();
    // The first arrival carries a ramp of its own, which must still never be a leg.
    track.front().ramp_seconds = 1.0;
    // A zero-ramp step after the move: arrives instantly, so it is never a leg either.
    track.push_back(
        HighwayHandArrival{
            .seconds = 8.0,
            .low_line = 1.0,
            .high_line = 5.0,
            .ramp_seconds = 0.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        });

    CHECK_FALSE(highwayHandLegAt(track, 1.5).has_value());
    CHECK_FALSE(highwayHandLegAt(track, 3.9).has_value());

    const std::optional<HighwayHandLeg> at_start = highwayHandLegAt(track, 4.0);
    REQUIRE(at_start.has_value());
    if (at_start.has_value())
    {
        CHECK(at_start->from == &track[0]);
        CHECK(at_start->to == &track[1]);
        CHECK(std::is_eq(at_start->progress <=> 0.0));
    }
    const std::optional<HighwayHandLeg> mid = highwayHandLegAt(track, 5.0);
    REQUIRE(mid.has_value());
    if (mid.has_value())
    {
        CHECK(std::is_eq(mid->progress <=> 0.5));
    }

    CHECK_FALSE(highwayHandLegAt(track, 6.0).has_value());
    CHECK_FALSE(highwayHandLegAt(track, 7.99).has_value());
    CHECK_FALSE(highwayHandLegAt(track, 8.0).has_value());
    CHECK_FALSE(highwayHandLegAt({}, 5.0).has_value());
}

// THE READING RULE: a placement whose ramp lies inside a dark gap between two lights moves
// neither. A light never starts a new leg outside its own stretch: after its release it holds the
// release's window, and through its rise it already stands where its start stands.
TEST_CASE("Lit window holds still outside its stretch", "[core][highway][window]")
{
    const auto arrival =
        [](const double seconds, const double low, const double high, const double ramp) {
            return HighwayHandArrival{
                .seconds = seconds,
                .low_line = low,
                .high_line = high,
                .ramp_seconds = ramp,
                .unpitched_ramp = false,
                .settle_seconds = 0.0,
            };
        };
    // Lines 2-6 from the start; a move to 7-11 ramping over 2.5-3.0, wholly inside the gap; and a
    // move to 1-5 ramping over 4.5-5.0, under the second light's rise.
    const std::vector<HighwayHandArrival> track{
        arrival(0.0, 2.0, 6.0, 0.0), arrival(3.0, 7.0, 11.0, 0.5), arrival(5.0, 1.0, 5.0, 0.5)
    };
    const HighwayLitStretch first{
        .start_seconds = 0.5, .release_seconds = 1.0, .rise_seconds = 0.0
    };
    const HighwayLitStretch second{
        .start_seconds = 5.0, .release_seconds = 6.5, .rise_seconds = 0.5
    };

    // The gap's move is real on the track itself.
    const HighwayHandWindow mid_gap = highwayHandWindowAt(track, 2.75);
    CHECK(mid_gap.low_line > 2.0);
    CHECK(mid_gap.low_line < 7.0);

    // The first light, after its release, holds its window at the release — through its decay and
    // through the whole gap.
    const HighwayHandWindow at_release = highwayLitWindowAt(track, first, 1.0);
    CHECK(at_release == HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
    CHECK(highwayLitWindowAt(track, first, 1.05) == at_release);
    CHECK(highwayLitWindowAt(track, first, 2.75) == at_release);

    // The second light, through its rise, already stands where its start stands, while the track
    // underneath is still mid-move.
    const HighwayHandWindow at_start = highwayLitWindowAt(track, second, 5.0);
    CHECK(at_start == HighwayHandWindow{.low_line = 1.0, .high_line = 5.0});
    CHECK(highwayLitWindowAt(track, second, 4.75) == at_start);
    CHECK(highwayHandWindowAt(track, 4.75).low_line > 1.0);

    // Inside its stretch a light reads the track as it is.
    CHECK(highwayLitWindowAt(track, second, 6.0) == highwayHandWindowAt(track, 6.0));
}

// A leg already in progress AT the release is still followed to its end — a slide-out settling
// over its crop zone keeps travelling while its light fades — and then holds; a leg that only
// begins inside the decay never moves the light.
TEST_CASE("Lit window finishes the leg in progress at its release", "[core][highway][window]")
{
    std::vector<HighwayHandArrival> track = makeSettlingPlacements(0.25);
    // A later move to lines 1-5 whose ramp begins after the slide-out has landed.
    track.push_back(
        HighwayHandArrival{
            .seconds = 7.0,
            .low_line = 1.0,
            .high_line = 5.0,
            .ramp_seconds = 0.5,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        });
    // Released halfway along the slide-out's one-second approach (5.0 to 6.0).
    const HighwayLitStretch light{
        .start_seconds = 2.0, .release_seconds = 5.5, .rise_seconds = 0.0
    };

    CHECK(highwayLitWindowAt(track, light, 5.75) == highwayHandWindowAt(track, 5.75));
    CHECK(std::is_eq(highwayLitTrackTime(track, light, 6.75) <=> 6.0));
    CHECK(
        highwayLitWindowAt(track, light, 6.75) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});
    CHECK_FALSE(highwayHandWindowAt(track, 6.75) == highwayLitWindowAt(track, light, 6.75));

    // With no leg in progress at the release, the light holds the release's window.
    const HighwayLitStretch early{
        .start_seconds = 2.0, .release_seconds = 4.5, .rise_seconds = 0.0
    };
    CHECK(std::is_eq(highwayLitTrackTime(track, early, 5.5) <=> 4.5));
    CHECK(
        highwayLitWindowAt(track, early, 5.5) ==
        HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
}

// THE ONE DENSITY POLICY, read through the sampler that applies it: a leg is sliced four times per
// fret of its wider edge's travel, never fewer than six so a sub-fret wiggle still reads as a
// curve, never more than sixty-four so a full-neck scrape cannot tessellate past a batch budget,
// and monotone in between so a longer travel never draws with fewer slices.
TEST_CASE("Track sampling slices a leg four per fret, bounded", "[core][highway][window]")
{
    // The slices one leg of `sweep` frets gets: a settled window, then a one-second ramp over
    // 1.0-2.0, sampled over [0.5, 3] so every slice falls inside — the samples past the range's two
    // ends and the arrival are the slices.
    const auto slices_for = [](const double sweep) {
        const std::vector<HighwayHandArrival> leg{
            HighwayHandArrival{
                .seconds = 0.0,
                .low_line = 0.0,
                .high_line = 4.0,
                .ramp_seconds = 0.0,
                .unpitched_ramp = false,
                .settle_seconds = 0.0,
            },
            HighwayHandArrival{
                .seconds = 2.0,
                .low_line = sweep,
                .high_line = 4.0 + sweep,
                .ramp_seconds = 1.0,
                .unpitched_ramp = false,
                .settle_seconds = 0.0,
            },
        };
        std::vector<double> times;
        highwayTrackSampleTimes(leg, 0.5, 3.0, times);
        highwaySortUniqueTimes(times);
        return static_cast<int>(times.size()) - 3;
    };

    // The floor holds for anything under one and a half frets of travel.
    CHECK(slices_for(0.25) == 6);
    CHECK(slices_for(1.0) == 6);

    // Four per fret past that.
    CHECK(slices_for(2.0) == 8);
    CHECK(slices_for(3.0) == 12);
    CHECK(slices_for(7.0) == 28);

    // The ceiling holds from sixteen frets of travel on, a scrape's whole-neck leg included.
    CHECK(slices_for(16.0) == 64);
    CHECK(slices_for(20.0) == 64);

    int previous = slices_for(0.25);
    for (int step = 1; step <= 40; ++step)
    {
        const int count = slices_for(static_cast<double>(step) / 2.0);
        CHECK(count >= previous);
        previous = count;
    }
}

// Sample instants closer than the onset epsilon are one moment: sorted, the repeat is dropped, so
// no two samples make a zero-length segment.
TEST_CASE("Sorted sample times drop instants one epsilon apart", "[core][highway][window]")
{
    std::vector<double> times{3.0, 1.0 + 1.0e-12, 2.0, 1.0, 3.0};
    highwaySortUniqueTimes(times);
    REQUIRE(times.size() == 3);
    CHECK(times[0] == Catch::Approx(1.0));
    CHECK(std::is_eq(times[1] <=> 2.0));
    CHECK(std::is_eq(times[2] <=> 3.0));
}

// THE ONE SAMPLING POLICY at its breakpoints: the range's ends are always sampled, and only once
// each even where an arrival sits on one; a settled stretch and an empty track add nothing past
// them; a ramp is sliced by the density policy only where it lies inside the range; and a leg that
// arrives instantly, or whose edges do not move, adds its arrival and no slices.
TEST_CASE("Track sample times slice only the ramps inside the range", "[core][highway][window]")
{
    // Lines 2-6 at 2.0, then a two-second ramp over 4.0-6.0 to lines 7-13: seven lines of travel.
    std::vector<HighwayHandArrival> track = makePlacements();
    const auto sampled = [&track](const double from_seconds, const double to_seconds) {
        std::vector<double> times;
        highwayTrackSampleTimes(track, from_seconds, to_seconds, times);
        highwaySortUniqueTimes(times);
        return times;
    };
    // Four slices per fret of the seven lines' travel.
    constexpr int slices = 28;
    const auto slice_at = [](const int slice) {
        return 4.0 + (2.0 * static_cast<double>(slice) / static_cast<double>(slices));
    };

    // An empty track and a settled stretch both give the range's ends and nothing else.
    std::vector<double> bare;
    highwayTrackSampleTimes({}, 1.0, 3.0, bare);
    CHECK(bare == std::vector<double>{1.0, 3.0});
    CHECK(sampled(0.5, 1.5) == std::vector<double>{0.5, 1.5});

    // An arrival AT the range's start is that start, appended once; the ramp after the range adds
    // nothing.
    std::vector<double> at_arrival;
    highwayTrackSampleTimes(track, 2.0, 3.0, at_arrival);
    CHECK(at_arrival == std::vector<double>{2.0, 3.0});

    // A ramp straddling the range's start is sliced only inside it: the slice landing exactly on
    // the start is the start itself, and the arrival inside the range is sampled.
    std::vector<double> straddling{5.0, 6.0, 7.0};
    for (int slice = 0; slice < slices; ++slice)
    {
        if (slice_at(slice) > 5.0)
        {
            straddling.push_back(slice_at(slice));
        }
    }
    std::ranges::sort(straddling);
    CHECK(sampled(5.0, 7.0) == straddling);

    // A ramp wholly inside the range contributes every slice.
    CHECK(sampled(3.0, 7.0).size() == static_cast<std::size_t>(slices) + 3);

    // A zero-ramp step and a ramp that moves no edge each add their arrival and no slices.
    track.push_back(
        HighwayHandArrival{
            .seconds = 8.0,
            .low_line = 1.0,
            .high_line = 5.0,
            .ramp_seconds = 0.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        });
    track.push_back(
        HighwayHandArrival{
            .seconds = 10.0,
            .low_line = 1.0,
            .high_line = 5.0,
            .ramp_seconds = 1.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        });
    CHECK(sampled(7.0, 11.0) == std::vector<double>{7.0, 8.0, 10.0, 11.0});
}

// THE BOX-SIDES RULE at its breakpoint: an approaching box stands at its hand's window at its own
// onset, even while the window is still moving now, and from the onset on it rides the live window.
TEST_CASE("Box sides hold the onset window, then ride the live one", "[core][highway][window]")
{
    const std::vector<HighwayHandArrival> track = makePlacements();

    // Before the onset (mid-ramp at 5.0), the box reads the window at its onset, not at now.
    CHECK(highwayBoxSidesAt(track, 5.0, 1.0) == highwayHandWindowAt(track, 5.0));
    CHECK_FALSE(highwayBoxSidesAt(track, 5.0, 1.0) == highwayHandWindowAt(track, 1.0));

    // At and after the onset, it reads the live window: still easing at 5.5, settled by 6.5.
    CHECK(highwayBoxSidesAt(track, 5.0, 5.0) == highwayHandWindowAt(track, 5.0));
    CHECK(highwayBoxSidesAt(track, 5.0, 5.5) == highwayHandWindowAt(track, 5.5));
    CHECK(
        highwayBoxSidesAt(track, 5.0, 6.5) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});
}

} // namespace rock_hero::common::core
