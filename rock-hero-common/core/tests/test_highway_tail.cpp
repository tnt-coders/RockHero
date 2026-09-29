#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <rock_hero/common/core/chart/bend_travel.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// A span vibrating at one width throughout: what the projection derives from a statement that never
// changes width.
[[nodiscard]] VibratoSpanViewState oneWidthSpan(
    const double start_seconds, const double end_seconds,
    const VibratoState width = VibratoState::Narrow)
{
    return VibratoSpanViewState{
        .start_seconds = start_seconds,
        .end_seconds = end_seconds,
        .state = width,
        .width_steps = {},
    };
}

// How far the board draws an unbent note above lane 1 of a six-lane stack, in lane gaps, for a
// vibrato displacement: the whole placement path, grid clamp included.
[[nodiscard]] double drawnGapsAboveLaneOne(const double vibrato_gaps)
{
    const HighwayMetrics metrics{};
    const double lane_1 = highwayLaneToY(1, metrics);
    const double drawn = highwayDrawnNoteY(
        lane_1,
        false,
        HighwayNoteOffset{.bend_semitones = 0.0, .vibrato_gaps = vibrato_gaps},
        6,
        metrics);
    return (drawn - lane_1) / metrics.string_distance;
}

// The one-sided slopes of a curve just before and just after an instant, for the kink checks.
template <typename Curve>
[[nodiscard]] std::pair<double, double> slopesAround(const Curve& curve, const double at)
{
    constexpr double h = 1.0e-6;
    return {(curve(at) - curve(at - h)) / h, (curve(at + h) - curve(at)) / h};
}

// A tail running straight along the board at 100 px/s: every stretch is sampled by its duration,
// which is what isolates the exact-time rules below from the travel-driven density.
[[nodiscard]] std::array<double, 2> straightTail(const double seconds)
{
    return {0.0, seconds * 100.0};
}

} // namespace

// Adaptive sampling (the per-millisecond-tessellation fix): density follows the projected
// screen length, bounded below by a drawable pair and above by the hard cap.
TEST_CASE("Highway tail sample count follows screen length under a cap", "[core][highway][tail]")
{
    CHECK(highwayTailSampleCount(0.0, 4.0, 256) == 2);
    CHECK(highwayTailSampleCount(-10.0, 4.0, 256) == 2);
    CHECK(highwayTailSampleCount(4.0, 4.0, 256) == 2);
    CHECK(highwayTailSampleCount(40.0, 4.0, 256) == 11);
    CHECK(highwayTailSampleCount(1.0e6, 4.0, 256) == 256);
    // Degenerate resolution never divides by zero.
    CHECK(highwayTailSampleCount(100.0, 0.0, 256) == 2);
}

// The taper envelope anchors modulated rails on the string line: zero at both ends, full
// amplitude through the middle, linear ramps over the taper fraction.
TEST_CASE("Highway tail taper anchors both ends", "[core][highway][tail]")
{
    CHECK(highwayTailTaper(0.0, 0.1) == Catch::Approx(0.0));
    CHECK(highwayTailTaper(1.0, 0.1) == Catch::Approx(0.0));
    CHECK(highwayTailTaper(0.05, 0.1) == Catch::Approx(0.5));
    CHECK(highwayTailTaper(0.1, 0.1) == Catch::Approx(1.0));
    CHECK(highwayTailTaper(0.5, 0.1) == Catch::Approx(1.0));
    CHECK(highwayTailTaper(0.95, 0.1) == Catch::Approx(0.5));
    // Out-of-range progress clamps instead of extrapolating.
    CHECK(highwayTailTaper(-1.0, 0.1) == Catch::Approx(0.0));
    CHECK(highwayTailTaper(2.0, 0.1) == Catch::Approx(0.0));
}

// Bend evaluation uses the same per-segment cosine ease as pitched slides, run on the drawn
// DISPLACEMENT (bendTravel), and hits every control point exactly. The ramp anchors at the onset
// unless the first point is a prebend at the onset itself.
TEST_CASE("Highway bend curve hits its control points exactly", "[core][highway][tail]")
{
    const std::vector<BendPointViewState> bend{
        BendPointViewState{.seconds = 11.0, .semitones = 2.0},
        BendPointViewState{.seconds = 12.0, .semitones = 1.0},
    };
    const auto travel_at = [&bend](const double seconds) {
        return bendTravel(highwayBendSemitonesAt(bend, 10.0, seconds));
    };

    CHECK(highwayBendSemitonesAt(bend, 10.0, 10.0) == Catch::Approx(0.0));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 11.0) == Catch::Approx(2.0));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 12.0) == Catch::Approx(1.0));
    // Halfway through a segment the travel is halfway between its ends', and one quarter into a
    // cosine segment has eased 0.1464466094 of the way.
    CHECK(travel_at(10.5) == Catch::Approx(bendTravel(2.0) / 2.0));
    CHECK(travel_at(11.5) == Catch::Approx((bendTravel(2.0) + bendTravel(1.0)) / 2.0));
    CHECK(travel_at(10.25) == Catch::Approx(bendTravel(2.0) * 0.1464466094));
    CHECK(travel_at(10.75) == Catch::Approx(bendTravel(2.0) * (1.0 - 0.1464466094)));
    CHECK(
        travel_at(11.25) ==
        Catch::Approx(bendTravel(2.0) - ((bendTravel(2.0) - bendTravel(1.0)) * 0.1464466094)));
    // After the last point the final value holds.
    CHECK(highwayBendSemitonesAt(bend, 10.0, 20.0) == Catch::Approx(1.0));
    // An empty curve is a flat zero.
    CHECK(highwayBendSemitonesAt({}, 10.0, 11.0) == Catch::Approx(0.0));

    // A prebend (first point at the onset) anchors the start value instead of ramping from zero.
    const std::vector<BendPointViewState> prebend{
        BendPointViewState{.seconds = 10.0, .semitones = 1.0},
        BendPointViewState{.seconds = 12.0, .semitones = 1.0},
    };
    CHECK(highwayBendSemitonesAt(prebend, 10.0, 10.0) == Catch::Approx(1.0));
    CHECK(highwayBendSemitonesAt(prebend, 10.0, 11.0) == Catch::Approx(1.0));
}

// A multi-stage bend that keeps rising comes to rest at its intermediate control point: a point
// at one inside a two-step bend says the bend STOPS there, and the shelf is what shows it. It
// stays monotone with no overshoot and holds plateaus exactly flat.
TEST_CASE("Highway bend curve eases through same-direction points", "[core][highway][tail]")
{
    const std::vector<BendPointViewState> rise{
        BendPointViewState{.seconds = 11.0, .semitones = 1.0},
        BendPointViewState{.seconds = 12.0, .semitones = 2.0},
    };
    CHECK(
        bendTravel(highwayBendSemitonesAt(rise, 10.0, 10.5)) ==
        Catch::Approx(bendTravel(1.0) / 2.0));
    CHECK(highwayBendSemitonesAt(rise, 10.0, 11.0) == Catch::Approx(1.0));
    CHECK(
        bendTravel(highwayBendSemitonesAt(rise, 10.0, 11.5)) ==
        Catch::Approx((bendTravel(1.0) + bendTravel(2.0)) / 2.0));
    // The cosine rule makes the intermediate point an eased arrival.
    const double just_before = highwayBendSemitonesAt(rise, 10.0, 11.0 - 0.01);
    const double just_after = highwayBendSemitonesAt(rise, 10.0, 11.0 + 0.01);
    CHECK(just_after - just_before < 0.001);
    // Monotone, and never past a control value.
    double previous = 0.0;
    for (int step = 0; step <= 40; ++step)
    {
        const double value = highwayBendSemitonesAt(rise, 10.0, 10.0 + (2.0 * step / 40.0));
        CHECK(value >= previous - 1.0e-12);
        CHECK(value <= 2.0 + 1.0e-12);
        previous = value;
    }

    // A GP-style plateau between two rises stays exactly flat inside the plateau.
    const std::vector<BendPointViewState> plateau{
        BendPointViewState{.seconds = 11.0, .semitones = 1.0},
        BendPointViewState{.seconds = 11.5, .semitones = 1.0},
        BendPointViewState{.seconds = 12.5, .semitones = 2.0},
    };
    CHECK(highwayBendSemitonesAt(plateau, 10.0, 11.1) == Catch::Approx(1.0));
    CHECK(highwayBendSemitonesAt(plateau, 10.0, 11.25) == Catch::Approx(1.0));
    CHECK(highwayBendSemitonesAt(plateau, 10.0, 11.4) == Catch::Approx(1.0));
}

// A BEND COMES TO REST AT THE UNBENT STRING TOO. The travel law is a square root there, so a curve
// eased in pitch reached the string line at an angle — a visible corner wherever a bend starts
// from rest or releases to it (sighted 2026-09-29). Eased in the drawn travel, the curve leaves and
// arrives flat: over a small step the travel moves second-order, not first.
TEST_CASE("Highway bend curve leaves and reaches the unbent string flat", "[core][highway][tail]")
{
    // At rest until 11.0, up a whole step by 12.0, released by 13.0.
    const std::vector<BendPointViewState> bend{
        BendPointViewState{.seconds = 11.0, .semitones = 0.0},
        BendPointViewState{.seconds = 12.0, .semitones = 2.0},
        BendPointViewState{.seconds = 13.0, .semitones = 0.0},
    };
    const auto travel_at = [&bend](const double seconds) {
        return bendTravel(highwayBendSemitonesAt(bend, 10.0, seconds));
    };
    constexpr double step = 1.0e-3;
    // A first-order departure would move the travel about step x its slope; this one moves it
    // step squared, three orders of magnitude less.
    CHECK(std::abs(travel_at(11.0 + step)) < 1.0e-4);
    CHECK(std::abs(travel_at(13.0 - step)) < 1.0e-4);
    CHECK(travel_at(13.0) == Catch::Approx(0.0).margin(1.0e-12));
}

// Bends on the upper half of the displayed stack invert so the curve stays inside the board;
// the middle lane of an odd stack keeps the upward default.
TEST_CASE("Highway bend inversion splits the displayed stack", "[core][highway][tail]")
{
    CHECK_FALSE(highwayBendInverted(1, 6));
    CHECK_FALSE(highwayBendInverted(3, 6));
    CHECK(highwayBendInverted(4, 6));
    CHECK(highwayBendInverted(6, 6));
    CHECK_FALSE(highwayBendInverted(3, 5));
    CHECK(highwayBendInverted(4, 5));
}

TEST_CASE("Highway bend inversion belongs to the onset group", "[core][highway][tail]")
{
    const auto note_on_string = [](const int string) {
        NoteViewState note;
        note.string = string;
        return note;
    };
    const std::vector<NoteViewState> notes{
        note_on_string(4),
        note_on_string(3),
        note_on_string(2),
        note_on_string(4),
        note_on_string(5),
    };

    const HighwayChordGroupViewState tied_g_and_d{.first = 0, .count = 2};
    CHECK(highwayBendInverted(notes, tied_g_and_d, 0, 6, false));

    const HighwayChordGroupViewState lower_majority_g_d_a{.first = 0, .count = 3};
    CHECK_FALSE(highwayBendInverted(notes, lower_majority_g_d_a, 0, 6, false));

    const HighwayChordGroupViewState upper_majority_g_b{.first = 3, .count = 2};
    CHECK(highwayBendInverted(notes, upper_majority_g_b, 0, 6, false));

    CHECK(highwayBendInverted(notes, lower_majority_g_d_a, 0, 6, true));
    CHECK_FALSE(highwayBendInverted(notes, upper_majority_g_b, 0, 6, true));
}

// The lift is the physical displacement law: n semitones needs the tension ratio 2^(n/6), and
// travel grows with the square root of the tension gain, anchored so a half step spans exactly
// one lane gap. Each extra semitone moves the string less than the one before, which is what
// makes the drawn shape read as a string being bent rather than a pitch plot.
TEST_CASE("Highway bend lift follows the tension displacement law", "[core][highway][tail]")
{
    const HighwayMetrics metrics{};
    const double gap = metrics.string_distance;

    // The half-step anchor is exact, and two fixed points pin the curve independently of the
    // implementation's own formula: a whole step travels sqrt((2^(1/3)-1)/(2^(1/6)-1)) = 1.45686
    // first-semitone units, and the three-whole-step ceiling — where the tension has doubled —
    // travels sqrt(1/(2^(1/6)-1)) = 2.85759.
    CHECK(highwayBendLiftY(1.0, metrics) == Catch::Approx(gap));
    CHECK(highwayBendLiftY(2.0, metrics) == Catch::Approx(1.45686 * gap).epsilon(1e-4));
    CHECK(highwayBendLiftY(6.0, metrics) == Catch::Approx(2.85759 * gap).epsilon(1e-4));

    // Concave everywhere in the authored range: each semitone adds less travel than the one
    // before, even as the force keeps climbing.
    for (int semitone = 2; semitone <= 6; ++semitone)
    {
        const double previous_step = highwayBendLiftY(static_cast<double>(semitone - 1), metrics) -
                                     highwayBendLiftY(static_cast<double>(semitone - 2), metrics);
        const double this_step = highwayBendLiftY(static_cast<double>(semitone), metrics) -
                                 highwayBendLiftY(static_cast<double>(semitone - 1), metrics);
        CHECK(this_step < previous_step);
    }

    // A negative offset mirrors the same curve, and the steep near-zero slope lifts a quarter
    // semitone almost half a lane gap.
    CHECK(highwayBendLiftY(-0.25, metrics) == Catch::Approx(-0.48916 * gap).epsilon(1e-3));
}

// A bent note never leaves the string grid. The floor is the origin and nothing draws beneath it,
// and nothing rises past the top fret line. Under the tension law the full three-whole-step
// ceiling fits the roomier side from every lane of a six-string stack, so the bounds only ever
// bite a malformed chart's junk semitones — which saturate at the edge rather than crossing it.
TEST_CASE("Highway bends stay inside the string grid", "[core][highway][tail]")
{
    const HighwayMetrics metrics{};
    const double grid_base = metrics.string_grid_base_y;
    const double grid_top = highwayStringGridTopY(6, metrics);

    // The ceiling fits without saturation from the tightest middle lanes, in both directions.
    const double lane_4 = highwayLaneToY(4, metrics);
    CHECK(highwayBendInverted(4, 6));
    const double ceiling_down =
        highwayDrawnNoteY(lane_4, true, HighwayNoteOffset{.bend_semitones = 6.0}, 6, metrics);
    CHECK(ceiling_down == Catch::Approx(lane_4 - highwayBendLiftY(6.0, metrics)));
    CHECK(ceiling_down > grid_base);

    const double lane_3 = highwayLaneToY(3, metrics);
    CHECK_FALSE(highwayBendInverted(3, 6));
    const double ceiling_up =
        highwayDrawnNoteY(lane_3, false, HighwayNoteOffset{.bend_semitones = 6.0}, 6, metrics);
    CHECK(ceiling_up == Catch::Approx(lane_3 + highwayBendLiftY(6.0, metrics)));
    CHECK(ceiling_up < grid_top);

    // Junk past the supported range saturates instead of leaving the grid.
    CHECK(
        highwayDrawnNoteY(lane_3, false, HighwayNoteOffset{.bend_semitones = 50.0}, 6, metrics) ==
        Catch::Approx(grid_top));
    CHECK(
        highwayDrawnNoteY(lane_4, true, HighwayNoteOffset{.bend_semitones = 50.0}, 6, metrics) ==
        Catch::Approx(grid_base));

    // The half-step anchor holds as a position: one semitone lands on the next lane up.
    const double lane_1 = highwayLaneToY(1, metrics);
    CHECK(
        highwayDrawnNoteY(lane_1, false, HighwayNoteOffset{.bend_semitones = 1.0}, 6, metrics) ==
        Catch::Approx(highwayLaneToY(2, metrics)));

    // A negative offset flips with its sign and is bounded on that side too.
    CHECK(
        highwayDrawnNoteY(lane_1, false, HighwayNoteOffset{.bend_semitones = -8.0}, 6, metrics) ==
        Catch::Approx(grid_base));
}

// The vibrato wobble is a displacement added AFTER the bend's tension curve, never a pitch fed
// through it, whose near-zero square root would flatten the crests and steepen the crossings. On
// an unbent note the drawn wobble is therefore a plain sine of the depth constant.
TEST_CASE("Highway vibrato draws a plain sine of its depth", "[core][highway][tail]")
{
    const HighwayMetrics metrics{};
    const double gap = metrics.string_distance;
    const double period = g_highway_vibrato_period_seconds;
    const double lane_1 = highwayLaneToY(1, metrics);
    // Long enough that the taper has finished at the instants read below.
    const std::vector<VibratoSpanViewState> whole_tail = {oneWidthSpan(0.0, 10.0)};
    const auto drawn_offset = [&](const double seconds) {
        return drawnGapsAboveLaneOne(highwayVibratoDisplacementAt(whole_tail, seconds, 1.0));
    };

    // Eighteen cycles (three seconds) in, clear of the one-second ramp: at 90 degrees the crest is
    // the constant, at 30 degrees exactly half of it — the ratio a square-rooted wave would put
    // near 0.71 instead.
    const double cycle_start = 18.0 * period;
    CHECK_THAT(
        drawn_offset(cycle_start + (period / 4.0)),
        Catch::Matchers::WithinAbs(g_highway_vibrato_depth_gaps, 1.0e-9));
    CHECK_THAT(
        drawn_offset(cycle_start + (period / 12.0)),
        Catch::Matchers::WithinAbs(g_highway_vibrato_depth_gaps / 2.0, 1.0e-9));
    CHECK_THAT(
        drawn_offset(cycle_start + (period * 3.0 / 4.0)),
        Catch::Matchers::WithinAbs(-g_highway_vibrato_depth_gaps, 1.0e-9));

    // The widest swing stays inside the half gap between an outer lane and the grid's edge, so an
    // unbent wobble never reaches the clamp that would flatten its crests.
    const double wide_swing =
        g_highway_vibrato_depth_gaps * g_highway_wide_vibrato_depth_multiplier;
    CHECK(lane_1 - (wide_swing * gap) > metrics.string_grid_base_y);
    CHECK(highwayLaneToY(6, metrics) + (wide_swing * gap) < highwayStringGridTopY(6, metrics));
}

// The grid's top edge closes the span the base opens, leaving the same half-string margin above the
// top lane that the base leaves below the bottom one.
TEST_CASE("Highway string grid top mirrors its base", "[core][highway][tail]")
{
    const HighwayMetrics metrics{};
    CHECK(
        highwayStringGridTopY(6, metrics) - highwayLaneToY(6, metrics) ==
        Catch::Approx(highwayLaneToY(1, metrics) - metrics.string_grid_base_y));

    // An empty chart still yields a one-lane grid rather than a collapsed one.
    CHECK(highwayStringGridTopY(0, metrics) == Catch::Approx(highwayStringGridTopY(1, metrics)));
}

// Slide easing endpoints are exact for both variants and the curves stay within [0, 1].
TEST_CASE("Highway slide easing spans its endpoints", "[core][highway][tail]")
{
    for (const bool unpitched : {false, true})
    {
        CHECK(highwaySlideEaseWeight(0.0, unpitched) == Catch::Approx(0.0).margin(1.0e-12));
        CHECK(highwaySlideEaseWeight(1.0, unpitched) == Catch::Approx(1.0).margin(1.0e-12));
        CHECK(highwaySlideEaseWeight(-1.0, unpitched) == Catch::Approx(0.0).margin(1.0e-12));
        CHECK(highwaySlideEaseWeight(2.0, unpitched) == Catch::Approx(1.0).margin(1.0e-12));
    }
    // The pitched curve is symmetric; the unpitched curve releases early.
    CHECK(highwaySlideEaseWeight(0.5, false) == Catch::Approx(0.5));
    CHECK(highwaySlideEaseWeight(0.5, true) < 0.5);
    CHECK(
        highwaySlideEaseWeight(0.25, false) ==
        Catch::Approx(1.0 - highwaySlideEaseWeight(0.75, false)));
}

// The slope is the curve's own derivative for both families: a central finite difference of the
// weight matches it, which is what lets the window's settle join the curve without a kink.
TEST_CASE("Highway slide ease slope is the weight's derivative", "[core][highway][tail]")
{
    constexpr double step = 1.0e-6;
    for (const bool unpitched : {false, true})
    {
        for (const double progress : {0.1, 0.35, 0.6, 0.85})
        {
            const double difference = (highwaySlideEaseWeight(progress + step, unpitched) -
                                       highwaySlideEaseWeight(progress - step, unpitched)) /
                                      (2.0 * step);
            CHECK(
                highwaySlideEaseSlope(progress, unpitched) ==
                Catch::Approx(difference).margin(1.0e-4));
        }
    }
}

// The Hermite basis passes through both endpoint values with both endpoint slopes, which a
// one-sided finite difference at each end confirms.
TEST_CASE("Cubic Hermite meets its endpoint values and slopes", "[core][highway][tail]")
{
    // From 2 with slope -3 to 7 with slope 0.5.
    const auto curve = [](const double t) { return cubicHermite(2.0, -3.0, 7.0, 0.5, t); };

    CHECK(curve(0.0) == Catch::Approx(2.0).margin(1.0e-12));
    CHECK(curve(1.0) == Catch::Approx(7.0).margin(1.0e-12));

    constexpr double step = 1.0e-6;
    CHECK((curve(step) - curve(0.0)) / step == Catch::Approx(-3.0).margin(1.0e-4));
    CHECK((curve(1.0) - curve(1.0 - step)) / step == Catch::Approx(0.5).margin(1.0e-4));

    // Equal values with zero slopes hold flat across the whole span.
    for (const double t : {0.25, 0.5, 0.75})
    {
        CHECK(cubicHermite(4.0, 0.0, 4.0, 0.0, t) == Catch::Approx(4.0).margin(1.0e-12));
    }
}

// Wobbles are pure functions phased from their own start: vibrato starts on the string line,
// tremolo peaks at the onset and swings the full depth each way.
TEST_CASE("Highway wobbles are start-phased and bounded", "[core][highway][tail]")
{
    const double period = g_highway_vibrato_period_seconds;
    CHECK(highwayVibratoWobble(0.0) == Catch::Approx(0.0).margin(1.0e-12));
    CHECK(highwayVibratoWobble(period / 4.0) == Catch::Approx(1.0).margin(1.0e-9));
    CHECK(highwayVibratoWobble(period) == Catch::Approx(0.0).margin(1.0e-9));

    const double depth = g_highway_tremolo_depth;
    CHECK(highwayTremoloWobble(0.0) == Catch::Approx(depth));
    CHECK(highwayTremoloWobble(0.5) == Catch::Approx(-depth));
    CHECK(highwayTremoloWobble(1.0) == Catch::Approx(depth));
    // The teeth swing wider than the ribbon is thick, so consecutive teeth clear each other.
    CHECK(depth > 1.0);
    for (int step = 0; step < 30; ++step)
    {
        const double wobble = highwayTremoloWobble(0.13 * step);
        CHECK(wobble >= -depth);
        CHECK(wobble <= depth);
    }
}

// The board's whole vibrato reading, now that the channel states SPANS: which one is in force, the
// envelope anchoring its wobble on the string line at that span's own ends, and the depth the
// caller shows. The old note-anchored arithmetic is the special case where the span is the whole
// tail, which is the identity every chart written before the channel could speak relies on.
TEST_CASE("Highway vibrato displacement follows the span in force", "[core][highway][tail]")
{
    const double period = g_highway_vibrato_period_seconds;
    const double depth = g_highway_vibrato_depth_gaps;
    // A four-second tail from 10.0s, vibrating end to end: exactly what the projection derives from
    // a chart stating vibrato at the onset and never restating it.
    const std::vector<VibratoSpanViewState> whole_tail = {oneWidthSpan(10.0, 14.0)};

    SECTION("a whole-tail span reproduces the note-anchored wobble exactly")
    {
        for (const double seconds : {10.4, 11.0, 12.0, 13.5, 13.9})
        {
            CAPTURE(seconds);
            const double from_onset = seconds - 10.0;
            const double expected =
                1.0 * highwayTailTaper(from_onset / 4.0, g_highway_tail_taper_fraction) * depth *
                highwayVibratoWobble(from_onset);
            // Bit-for-bit, not merely close: the whole frozen-visuals claim for the 3D surface is
            // that this arithmetic did not change for content that states nothing mid-ring.
            CHECK_THAT(
                highwayVibratoDisplacementAt(whole_tail, seconds, 1.0),
                Catch::Matchers::WithinULP(expected, 0));
        }
    }

    SECTION("the wobble anchors on the string line at the span's own ends")
    {
        CHECK_THAT(
            highwayVibratoDisplacementAt(whole_tail, 10.0, 1.0),
            Catch::Matchers::WithinAbs(0.0, 1.0e-12));
        CHECK_THAT(
            highwayVibratoDisplacementAt(whole_tail, 14.0, 1.0),
            Catch::Matchers::WithinAbs(0.0, 1.0e-12));
        // ...and nowhere near it in between, or the two ends above would prove nothing.
        CHECK(std::abs(highwayVibratoDisplacementAt(whole_tail, 10.0 + (period / 4.0), 1.0)) > 0.0);
    }

    SECTION("a time outside every span moves nothing")
    {
        CHECK_THAT(
            highwayVibratoDisplacementAt(whole_tail, 9.9, 1.0), Catch::Matchers::WithinULP(0.0, 0));
        CHECK_THAT(
            highwayVibratoDisplacementAt(whole_tail, 14.1, 1.0),
            Catch::Matchers::WithinULP(0.0, 0));
        CHECK_THAT(highwayVibratoDisplacementAt({}, 12.0, 1.0), Catch::Matchers::WithinULP(0.0, 0));
    }

    SECTION("a span stated mid-ring phases and tapers from its own start")
    {
        // The same note, but the vibrato begins part way in — deliberately NOT a whole number of
        // periods after the onset, so the two anchors genuinely disagree. At a quarter period past
        // THAT start the wave stands at its positive crest.
        const double late_start = 12.05;
        const double late_end = 14.0;
        const std::vector<VibratoSpanViewState> late = {oneWidthSpan(late_start, late_end)};
        const double crest_seconds = late_start + (period / 4.0);
        const double crest = highwayVibratoDisplacementAt(late, crest_seconds, 1.0);
        const double expected_taper = highwayTailTaper(
            (period / 4.0) / (late_end - late_start), g_highway_tail_taper_fraction);
        CHECK(crest == Catch::Approx(expected_taper * depth));
        // The discrimination: an onset-phased reading at the same instant is nowhere near the
        // crest, so this cannot pass by accident.
        CHECK(std::abs(highwayVibratoWobble(crest_seconds - 10.0) - 1.0) > 0.1);
        // Before the span starts there is no vibrato at all, however long the note has rung.
        CHECK_THAT(
            highwayVibratoDisplacementAt(late, 11.0, 1.0), Catch::Matchers::WithinULP(0.0, 0));
    }

    SECTION("the span in force is the one containing the time, not the first one")
    {
        const std::vector<VibratoSpanViewState> two = {
            oneWidthSpan(10.0, 11.0),
            oneWidthSpan(12.05, 14.0),
        };
        // Between the two the string is steady, however long it vibrated on either side.
        CHECK_THAT(
            highwayVibratoDisplacementAt(two, 11.5, 1.0), Catch::Matchers::WithinULP(0.0, 0));
        // Inside the second, both the wave's phase and its envelope come from THAT span: a
        // quarter period past its start is its crest. Reading the first span's anchor here
        // would run the envelope past its end and hold the wobble at nothing.
        const double crest_seconds = 12.05 + (period / 4.0);
        const double expected_taper =
            highwayTailTaper((period / 4.0) / (14.0 - 12.05), g_highway_tail_taper_fraction);
        CHECK(
            highwayVibratoDisplacementAt(two, crest_seconds, 1.0) ==
            Catch::Approx(expected_taper * depth));
        CHECK(expected_taper > 0.0);
    }

    SECTION("the head draws its fraction of the tail's drawn swing")
    {
        const auto drawn_offset = [&](const double seconds, const double depth_scale) {
            return drawnGapsAboveLaneOne(
                highwayVibratoDisplacementAt(whole_tail, seconds, depth_scale));
        };
        for (const double seconds : {10.05, 11.1, 12.3, 13.95})
        {
            CAPTURE(seconds);
            const double tail = drawn_offset(seconds, 1.0);
            CHECK(std::abs(tail) > 0.0);
            CHECK_THAT(
                drawn_offset(seconds, g_highway_vibrato_head_depth_fraction),
                Catch::Matchers::WithinAbs(g_highway_vibrato_head_depth_fraction * tail, 1.0e-12));
        }
    }

    SECTION("the wide tier swings the multiplier past the ordinary one")
    {
        // Read off the CONSTANT rather than off a copy of its value: the whole point of stating
        // the tier as a multiplier is that re-sighting the ordinary depth carries the exaggeration
        // with it, and a test asserting a literal would pass while that coupling was broken.
        const std::vector<VibratoSpanViewState> wide = {oneWidthSpan(
            10.0, 14.0, VibratoState::Wide)};
        for (const double seconds : {10.4, 11.0, 12.0, 13.5})
        {
            CAPTURE(seconds);
            CHECK_THAT(
                highwayVibratoDisplacementAt(wide, seconds, 1.0),
                Catch::Matchers::WithinAbs(
                    g_highway_wide_vibrato_depth_multiplier *
                        highwayVibratoDisplacementAt(whole_tail, seconds, 1.0),
                    1.0e-12));
        }
        // The spans above differ in NOTHING but their width, and the ordinary one really moves,
        // so the equality above is a scaling rather than two zeroes agreeing.
        CHECK(std::abs(highwayVibratoDisplacementAt(whole_tail, 12.0, 1.0)) > 0.0);
        CHECK(g_highway_wide_vibrato_depth_multiplier > 1.0);
    }

    SECTION("a span with no duration moves nothing instead of dividing by zero")
    {
        // A statement landing exactly on the end the note presents: the projection keeps it, and
        // the reading has to hold still rather than produce a NaN across the whole tail.
        const std::vector<VibratoSpanViewState> pinned = {oneWidthSpan(14.0, 14.0)};
        CHECK_THAT(
            highwayVibratoDisplacementAt(pinned, 14.0, 1.0), Catch::Matchers::WithinULP(0.0, 0));
    }
}

// A change of width inside a span is ONE wave changing its swing, not two waves: the phase runs on
// from the span's start, the envelope tapers only at the span's true ends, and the swing follows
// vibratoWideWeightAt between the board's two widths — smooth through the change at whatever phase
// it lands, however close the next change follows.
TEST_CASE("Highway vibrato changes width inside one wave", "[core][highway][tail]")
{
    const double narrow = g_highway_vibrato_depth_gaps;
    const double wide = g_highway_vibrato_depth_gaps * g_highway_wide_vibrato_depth_multiplier;
    const double ease = g_highway_vibrato_width_ease_seconds;
    // Deliberately off every crest and crossing, so an instant change would break the curve.
    const double step_seconds = 11.03;
    const std::vector<VibratoSpanViewState> spans = {VibratoSpanViewState{
        .start_seconds = 10.0,
        .end_seconds = 14.0,
        .state = VibratoState::Narrow,
        .width_steps = {
            VibratoWidthStepViewState{.seconds = step_seconds, .state = VibratoState::Wide}
        },
    }};
    const auto displacement = [&](const double seconds) {
        return highwayVibratoDisplacementAt(spans, seconds, 1.0);
    };
    // The wave's own phase, measured from the SPAN's start on both sides of the keyframe.
    const auto wobble = [](const double seconds) { return highwayVibratoWobble(seconds - 10.0); };
    CHECK(std::abs(wobble(step_seconds)) > 0.3);

    SECTION("the change begins at the keyframe with no taper and no restarted phase")
    {
        // Narrow up to the keyframe, and at it: full narrow swing times the span-phased sine. A
        // taper or a restart there would put the wave on the string line instead.
        CHECK_THAT(
            displacement(step_seconds),
            Catch::Matchers::WithinAbs(narrow * wobble(step_seconds), 1.0e-12));
        CHECK_THAT(
            displacement(step_seconds - 0.1),
            Catch::Matchers::WithinAbs(narrow * wobble(step_seconds - 0.1), 1.0e-12));
    }

    SECTION("the swing is the new width once the ease after the keyframe is done")
    {
        const double settled = step_seconds + ease;
        for (const double seconds : {settled, settled + 0.05, 12.5})
        {
            CAPTURE(seconds);
            CHECK_THAT(
                displacement(seconds), Catch::Matchers::WithinAbs(wide * wobble(seconds), 1.0e-12));
        }
        // Part way through the ease the swing lies strictly between the two widths.
        const double midway = step_seconds + (ease / 2.0);
        REQUIRE(std::abs(wobble(midway)) > 0.1);
        const double midway_swing = displacement(midway) / wobble(midway);
        CHECK(midway_swing > narrow);
        CHECK(midway_swing < wide);
    }

    SECTION("the value and its slope are continuous across the keyframe")
    {
        CHECK_THAT(
            displacement(step_seconds + 1.0e-6) - displacement(step_seconds - 1.0e-6),
            Catch::Matchers::WithinAbs(0.0, 1.0e-4));
        const auto [slope_in, slope_out] = slopesAround(displacement, step_seconds);
        // The wave is moving steeply here, so a matching slope is not two flat stretches agreeing.
        CHECK(std::abs(slope_in) > 1.0);
        CHECK_THAT(slope_out, Catch::Matchers::WithinAbs(slope_in, 1.0e-2));
        // ...and the ease's far end joins the settled wide wave with no kink either.
        const auto [settle_in, settle_out] = slopesAround(displacement, step_seconds + ease);
        CHECK_THAT(settle_out, Catch::Matchers::WithinAbs(settle_in, 1.0e-2));
    }

    SECTION("a step back inside the ease keeps the wave smooth")
    {
        // Back to narrow 0.03 s after the step to wide, well inside that step's ease: the second
        // ease begins while the first is still moving, and the wave carries on without a kink at
        // either end of either ease.
        const double back_seconds = step_seconds + 0.03;
        const std::vector<VibratoSpanViewState> blip = {VibratoSpanViewState{
            .start_seconds = 10.0,
            .end_seconds = 14.0,
            .state = VibratoState::Narrow,
            .width_steps = {
                VibratoWidthStepViewState{.seconds = step_seconds, .state = VibratoState::Wide},
                VibratoWidthStepViewState{.seconds = back_seconds, .state = VibratoState::Narrow},
            },
        }};
        const auto blip_displacement = [&](const double seconds) {
            return highwayVibratoDisplacementAt(blip, seconds, 1.0);
        };
        // The discrimination: the wave is moving steeply where the second ease begins, so a swing
        // whose own slope jumped there would kink the drawn curve visibly.
        REQUIRE(std::abs(wobble(back_seconds)) > 0.5);
        const double first_settled = step_seconds + ease;
        const double back_settled = back_seconds + ease;
        for (const double at : {step_seconds, back_seconds, first_settled, back_settled})
        {
            CAPTURE(at);
            const auto [slope_in, slope_out] = slopesAround(blip_displacement, at);
            CHECK_THAT(slope_out, Catch::Matchers::WithinAbs(slope_in, 1.0e-2));
        }
        // Narrow again once both eases are done.
        const double after = back_seconds + ease + 0.05;
        CHECK_THAT(
            blip_displacement(after), Catch::Matchers::WithinAbs(narrow * wobble(after), 1.0e-12));
    }
}

// Teeth step a fixed span of the note's own duration, so the count is the tail's length over that
// span and nothing else: a longer note carries proportionally more ridges, and no camera, distance
// or scroll-speed term appears anywhere. This pins the property the whole law exists for — the
// teeth belong to the note, so the same tail always shows the same ridges.
TEST_CASE("Highway tremolo teeth step a fixed span of tail time", "[core][highway][tail]")
{
    CHECK(highwayTremoloTailCycles(0.0) == Catch::Approx(0.0));

    // Count is strictly proportional to tail length: twice the tail, twice the ridges.
    const double single = highwayTremoloTailCycles(g_highway_tremolo_tooth_cycle_seconds);
    CHECK(single == Catch::Approx(1.0));
    CHECK(
        highwayTremoloTailCycles(4.0 * g_highway_tremolo_tooth_cycle_seconds) ==
        Catch::Approx(4.0));
    CHECK(
        highwayTremoloTailCycles(9.0 * g_highway_tremolo_tooth_cycle_seconds) ==
        Catch::Approx(9.0));

    // The time lookup inverts the phase exactly, which is what lets a caller place turning points
    // on the wave rather than near them, and makes the half-cycle walk terminate.
    for (int index = 0; index < 12; ++index)
    {
        const double cycles = 0.5 * index;
        CHECK(
            highwayTremoloTailCycles(highwayTremoloTailSecondsAtCycle(cycles)) ==
            Catch::Approx(cycles));
        // Every half cycle is a true extremum, so a sampler walking them lands the wave's
        // corners exactly rather than near them.
        CHECK(std::abs(highwayTremoloWobble(cycles)) == Catch::Approx(g_highway_tremolo_depth));
    }
    CHECK(highwayTremoloTailSecondsAtCycle(1.0) > highwayTremoloTailSecondsAtCycle(0.5));
}

// The teeth ease off the string line over a fixed number of TEETH at each end, so the run
// reads uniform whatever the sustain's length — a duration-fraction ramp instead damps a dozen
// teeth near the head of a long tail and under one on a short tail.
TEST_CASE("Highway tremolo envelope ramps in teeth, not duration", "[core][highway][tail]")
{
    const double ramp = g_highway_tremolo_ramp_cycles;

    // Anchored at both ends, full depth everywhere between.
    CHECK(highwayTremoloEnvelope(0.0, 40.0) == Catch::Approx(0.0));
    CHECK(highwayTremoloEnvelope(40.0, 40.0) == Catch::Approx(0.0));
    CHECK(highwayTremoloEnvelope(ramp, 40.0) == Catch::Approx(1.0));
    CHECK(highwayTremoloEnvelope(20.0, 40.0) == Catch::Approx(1.0));
    CHECK(highwayTremoloEnvelope(40.0 - ramp, 40.0) == Catch::Approx(1.0));
    CHECK(highwayTremoloEnvelope(ramp / 2.0, 40.0) == Catch::Approx(0.5));

    // The ramp costs the same teeth on a long tail as on a short one: past the first tooth
    // every tail is at full depth, however many teeth it carries.
    for (const double end : {8.0, 40.0, 200.0})
    {
        CHECK(highwayTremoloEnvelope(ramp, end) == Catch::Approx(1.0));
        CHECK(highwayTremoloEnvelope(end / 2.0, end) == Catch::Approx(1.0));
    }

    // Degenerate spans stay in range rather than exploding.
    CHECK(highwayTremoloEnvelope(0.0, 0.0) == Catch::Approx(0.0));
    CHECK(highwayTremoloEnvelope(5.0, 1.0) == Catch::Approx(0.0));
}

// The wobble rate is FIXED: the same period whatever the song's tempo or meter, because a
// vibrato's speed is the player's hand rather than the grid. Pins the rate inside the real
// 4-7 Hz vibrato band, and pins that a whole period elapses in exactly that time.
TEST_CASE("Highway vibrato runs at one fixed rate", "[core][highway][tail]")
{
    const double frequency = 1.0 / g_highway_vibrato_period_seconds;
    CHECK(frequency >= 4.0);
    CHECK(frequency <= 7.0);

    // Span-phased: zero at the span's start, back to zero after exactly one period, and at its
    // extremes a quarter and three quarters of the way through.
    const double period = g_highway_vibrato_period_seconds;
    CHECK(highwayVibratoWobble(0.0) == Catch::Approx(0.0).margin(1.0e-12));
    CHECK(highwayVibratoWobble(period / 4.0) == Catch::Approx(1.0));
    CHECK(highwayVibratoWobble(period * 3.0 / 4.0) == Catch::Approx(-1.0));
    CHECK(highwayVibratoWobble(period) == Catch::Approx(0.0).margin(1.0e-12));
}

// The turning-point pair is the ONE statement of where the span-anchored sine peaks: the renderer
// pins a sample to each extreme so the drawn wave stays rigid on the note, and it must land on the
// same phase highwayVibratoDisplacementAt reads. Skew either direction against the other and the
// samples slide off the wave they are meant to trace, which is exactly the silent aliasing having
// two copies of the anchor rule invited.
TEST_CASE("Highway vibrato turning points invert the wobble phase", "[core][highway][tail]")
{
    const double period = g_highway_vibrato_period_seconds;

    // Index zero is the first crest, a quarter period into the span; each index after it steps
    // one half period, and the span's own start sits half an index before that first crest.
    CHECK(highwayVibratoSecondsAtTurningIndex(0.0) == Catch::Approx(period / 4.0));
    CHECK(highwayVibratoSecondsAtTurningIndex(1.0) == Catch::Approx(period * 3.0 / 4.0));
    CHECK(highwayVibratoTurningIndex(0.0) == Catch::Approx(-0.5));

    // The index lookup inverts the time lookup exactly, which is what lets a caller place turning
    // points on the wave rather than near them, and makes the walk over them terminate.
    for (int index = 0; index < 12; ++index)
    {
        const auto turning = static_cast<double>(index);
        const double seconds = highwayVibratoSecondsAtTurningIndex(turning);
        CHECK(highwayVibratoTurningIndex(seconds) == Catch::Approx(turning));
        // Every whole index is a true extremum, so a sampler walking them lands the wave's
        // corners exactly rather than near them.
        CHECK(std::abs(highwayVibratoWobble(seconds)) == Catch::Approx(1.0));
    }
    CHECK(highwayVibratoSecondsAtTurningIndex(1.0) > highwayVibratoSecondsAtTurningIndex(0.0));
}

// Sample times cover the span, include every technique control point inside it exactly, and
// stay sorted and deduplicated.
TEST_CASE("Highway tail sample times include control points", "[core][highway][tail]")
{
    NoteViewState note;
    note.start_seconds = 10.0;
    note.ring_end_seconds = 14.0;
    note.ink_end_seconds = 14.0;
    note.bend = {
        BendPointViewState{.seconds = 11.3, .semitones = 1.0},
        BendPointViewState{.seconds = 9.0, .semitones = 0.5},  // outside: dropped
        BendPointViewState{.seconds = 15.0, .semitones = 0.0}, // outside: dropped
    };
    note.slides = {SlideStopViewState{.seconds = 12.7, .fret = 7}};

    const std::vector<double> times =
        makeHighwayTailSampleTimes(note, 10.0, 14.0, straightTail, 4.0, {}, 256);

    REQUIRE(times.size() >= 5);
    CHECK(times.front() == Catch::Approx(10.0));
    CHECK(times.back() == Catch::Approx(14.0));
    CHECK(std::ranges::is_sorted(times));
    CHECK(std::ranges::count(times, 11.3) == 1);
    CHECK(std::ranges::count(times, 12.7) == 1);
    for (std::size_t index = 1; index < times.size(); ++index)
    {
        CHECK(times[index] - times[index - 1] > 0.0);
    }

    // An empty span yields no samples.
    CHECK(makeHighwayTailSampleTimes(note, 12.0, 12.0, straightTail, 4.0, {}, 256).empty());
}

// The caller's extra sample times survive into the list exactly, which is what keeps a teethed
// tail's corners crisp: the triangle is linear between its turning points, so samples between them
// alone would round every apex and alias the wave at this tooth spacing.
TEST_CASE("Highway tail sample times keep the caller's extra times", "[core][highway][tail]")
{
    NoteViewState note;
    note.start_seconds = 10.0;
    note.ring_end_seconds = 11.0;
    note.ink_end_seconds = 11.0;

    const std::vector<double> extra{10.13, 10.42, 10.87};
    const std::vector<double> times =
        makeHighwayTailSampleTimes(note, 10.0, 11.0, straightTail, 4.0, extra, 256);

    CHECK(std::ranges::is_sorted(times));
    for (const double wanted : extra)
    {
        const auto hits = std::ranges::count_if(
            times, [&](const double value) { return std::abs(value - wanted) < 1.0e-9; });
        CHECK(hits == 1);
    }
    // Extras outside the span are dropped rather than widening it.
    const std::vector<double> outside{9.0, 10.5, 12.0};
    const std::vector<double> clipped =
        makeHighwayTailSampleTimes(note, 10.0, 11.0, straightTail, 4.0, outside, 256);
    CHECK(clipped.front() == Catch::Approx(10.0));
    CHECK(clipped.back() == Catch::Approx(11.0));
    CHECK(std::ranges::count_if(clipped, [](const double value) {
              return std::abs(value - 10.5) < 1.0e-9;
          }) == 1);

    // Sampling extras stays optional: a plain tail passes none and is sampled by its travel alone,
    // exactly the count its 100 on-screen pixels ask for.
    const std::vector<double> plain =
        makeHighwayTailSampleTimes(note, 10.0, 11.0, straightTail, 4.0, {}, 256);
    CHECK(plain.size() == highwayTailSampleCount(100.0, 4.0, 256));
}

// THE SAMPLES GO WHERE THE CURVE MOVES. A quick slide at the end of a long hold crosses most of its
// on-screen travel in a sliver of the tail's time; spent by duration, it got a sample or three and
// drew its eased S-curve as straight legs with corners (sighted 2026-09-29). Each stretch between
// exact times now takes its own travel's count, so the glide gets samples in proportion to how far
// it moves.
TEST_CASE("Highway tail sample times gather where the curve travels", "[core][highway][tail]")
{
    NoteViewState note;
    note.start_seconds = 10.0;
    note.ring_end_seconds = 14.0;
    note.ink_end_seconds = 14.0;
    // A hold to 12.0, then a glide over a tenth of a second.
    note.slides = {
        SlideStopViewState{.seconds = 12.0, .fret = 5},
        SlideStopViewState{.seconds = 12.1, .fret = 9},
    };
    // Slow along the board, 10 px/s; 400 px sideways across the glide.
    const auto sliding = [](const double seconds) {
        const double glide = std::clamp((seconds - 12.0) / 0.1, 0.0, 1.0);
        return std::array<double, 2>{glide * 400.0, (seconds - 10.0) * 10.0};
    };

    const std::vector<double> times =
        makeHighwayTailSampleTimes(note, 10.0, 14.0, sliding, 4.0, {}, 256);
    const auto inside_glide = std::ranges::count_if(
        times, [](const double value) { return value > 12.0 && value < 12.1; });
    // Its 400 px ask for a hundred samples, where the hold's 20 px ask for five.
    CHECK(inside_glide >= 90);
    CHECK(times.size() < 120);
}

// The cap is one budget for the whole list. The exact times are never evicted — a turning point
// the grid rounds is a visible error — so the in-between samples are what yield, down to none.
// Before this the cap bounded only the grid, every exact time was appended past it, and a long
// teethed open tail reached nearly twice the cap.
TEST_CASE("Highway tail sample times hold the cap as one budget", "[core][highway][tail]")
{
    NoteViewState note;
    note.start_seconds = 10.0;
    note.ring_end_seconds = 20.0;
    note.ink_end_seconds = 20.0;

    // Forty exact times under a cap of 64: the list holds the cap.
    std::vector<double> extra;
    for (int index = 1; index <= 40; ++index)
    {
        extra.push_back(10.0 + (static_cast<double>(index) * 0.2));
    }
    const std::vector<double> budgeted =
        makeHighwayTailSampleTimes(note, 10.0, 20.0, straightTail, 4.0, extra, 64);
    CHECK(budgeted.size() <= 64);
    CHECK(budgeted.front() == Catch::Approx(10.0));
    CHECK(budgeted.back() == Catch::Approx(20.0));
    for (const double wanted : extra)
    {
        CHECK(std::ranges::count_if(budgeted, [&](const double value) {
                  return std::abs(value - wanted) < 1.0e-9;
              }) == 1);
    }

    // More exact times than the whole budget: every one survives, and nothing between them.
    const std::vector<double> flooded =
        makeHighwayTailSampleTimes(note, 10.0, 20.0, straightTail, 4.0, extra, 16);
    CHECK(flooded.size() == extra.size() + 2);
    CHECK(flooded.front() == Catch::Approx(10.0));
    CHECK(flooded.back() == Catch::Approx(20.0));
}

} // namespace rock_hero::common::core
