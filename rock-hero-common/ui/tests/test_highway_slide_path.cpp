#include "highway/highway_slide_path.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_tail.h>

namespace rock_hero::common::ui
{

namespace
{

// A fretted note at fret 5 starting at one second, with no gesture of its own yet. Every case
// below adds the waypoints it needs, so the shared part stays the note the board would draw.
[[nodiscard]] common::core::NoteViewState frettedNote()
{
    common::core::NoteViewState note;
    note.string = 1;
    note.fret = 5;
    note.start_seconds = 1.0;
    note.end_seconds = 4.0;
    return note;
}

// One glide waypoint; `unpitched` selects the release family rather than a pitched arrival.
[[nodiscard]] common::core::SlideViewState waypoint(
    const double seconds, const int fret, const bool unpitched = false)
{
    return common::core::SlideViewState{
        .seconds = seconds,
        .fret = fret,
        .unpitched = unpitched,
    };
}

} // namespace

// The anchor every point of a gesture is placed against. A plain stop takes its fret slot's
// middle, and a harmonic's head sits on the NODE instead — the split the board and the 2D label
// both obey, so a glide cannot arrive somewhere the two surfaces disagree about.
TEST_CASE("Highway fretboard anchor takes the slot, or the node when there is one", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    const common::core::NoteViewState note = frettedNote();
    CHECK_THAT(
        highwayNoteFretboardX(note, note.fret, metrics, false),
        Catch::Matchers::WithinULP(common::core::highwayNoteCenterX(5, metrics, false), 0));

    // An artificial harmonic pressed at fret 5 sounding at node 7.2 draws on the node's exact
    // fractional line, not in a fret slot.
    common::core::NoteViewState harmonic = frettedNote();
    harmonic.harmonic_node = 7.2;
    CHECK_THAT(
        highwayNoteFretboardX(harmonic, harmonic.fret, metrics, false),
        Catch::Matchers::WithinULP(common::core::highwayFretLineX(7.2, metrics, false), 0));
}

// The two ends of a glide segment: nothing has moved at the onset, and the offset at a waypoint's
// own time is exactly that waypoint's anchor. A mark walking this path therefore starts under its
// head and lands on the fret the chart says the gesture arrives at.
TEST_CASE("Glide holds the onset anchor at the onset and the target at a waypoint", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.slides = {waypoint(2.0, 9)};
    const double base_x = highwayNoteFretboardX(note, note.fret, metrics, false);

    const HighwaySlideState at_onset =
        highwaySlideStateAt(note, base_x, metrics, false, note.start_seconds);
    CHECK_THAT(at_onset.x_offset, Catch::Matchers::WithinULP(0.0, 0));
    CHECK_THAT(at_onset.alpha, Catch::Matchers::WithinULP(1.0, 0));

    const double target_x = highwayNoteFretboardX(note, 9, metrics, false);
    const HighwaySlideState at_waypoint = highwaySlideStateAt(note, base_x, metrics, false, 2.0);
    CHECK_THAT(base_x + at_waypoint.x_offset, Catch::Matchers::WithinAbs(target_x, 1e-12));
}

// Mid-segment the path is the EASED interpolation, in the family the arriving waypoint names —
// the same weights the tail's own centerline and the tapping hand's light travel by. A pitched
// glide accelerates into its target where an unpitched release leaves early, so the two are
// measurably apart at the same instant.
TEST_CASE("Mid-glide the path is the eased weight, pitched and unpitched apart", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState pitched = frettedNote();
    pitched.slides = {waypoint(2.0, 9)};
    common::core::NoteViewState unpitched = frettedNote();
    unpitched.slides = {waypoint(2.0, 9, true)};

    const double base_x = highwayNoteFretboardX(pitched, pitched.fret, metrics, false);
    const double target_x = highwayNoteFretboardX(pitched, 9, metrics, false);
    const double travel = target_x - base_x;

    const double pitched_offset =
        highwaySlideStateAt(pitched, base_x, metrics, false, 1.5).x_offset;
    const double unpitched_offset =
        highwaySlideStateAt(unpitched, base_x, metrics, false, 1.5).x_offset;
    CHECK_THAT(
        pitched_offset,
        Catch::Matchers::WithinAbs(
            travel * common::core::highwaySlideEaseWeight(0.5, false), 1e-12));
    CHECK_THAT(
        unpitched_offset,
        Catch::Matchers::WithinAbs(
            travel * common::core::highwaySlideEaseWeight(0.5, true), 1e-12));
    CHECK(unpitched_offset < pitched_offset);
}

// Past the last waypoint the glide HOLDS its target, which is what "continue straight along the
// fret it stopped on" means: a gesture that has stopped travelling does not drift, and anything
// drawn past the last waypoint reads the fret it stopped on.
TEST_CASE("Past the last waypoint the glide holds its final target", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.slides = {waypoint(2.0, 9), waypoint(3.0, 12)};
    const double base_x = highwayNoteFretboardX(note, note.fret, metrics, false);
    const double final_x = highwayNoteFretboardX(note, 12, metrics, false);

    for (const double seconds : {3.0, 5.0, 60.0})
    {
        const HighwaySlideState held = highwaySlideStateAt(note, base_x, metrics, false, seconds);
        CHECK_THAT(base_x + held.x_offset, Catch::Matchers::WithinAbs(final_x, 1e-12));
        // A pitched arrival keeps the note's full brightness after it lands.
        CHECK_THAT(held.alpha, Catch::Matchers::WithinULP(1.0, 0));
    }
}

// A harmonic's node RIDES its stop: fret spacing is logarithmic, so the node keeps a constant
// offset in fret units above whatever the glide has travelled to. One rule places the onset and
// every station of the glide, which is why the anchor takes the stop as a parameter.
TEST_CASE("A harmonic's node rides its stop through a glide", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.harmonic_node = 7.2;
    note.slides = {waypoint(2.0, 9)};
    const double base_x = highwayNoteFretboardX(note, note.fret, metrics, false);

    // Four frets of travel move the node four fret units up, to 11.2.
    const HighwaySlideState arrived = highwaySlideStateAt(note, base_x, metrics, false, 2.0);
    CHECK_THAT(
        base_x + arrived.x_offset,
        Catch::Matchers::WithinAbs(common::core::highwayFretLineX(11.2, metrics, false), 1e-12));
}

// The unpitched release's dim spans the whole CONSECUTIVE run, not each leg: a scrape's chained
// legs are one continuous release, so the alpha must never snap back to full where the travel
// reverses. Only the geometry restarts per leg.
TEST_CASE("The unpitched dim ramps across the whole consecutive run", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState scrape = frettedNote();
    scrape.slides = {waypoint(2.0, 10, true), waypoint(3.0, 3, true)};
    const double base_x = highwayNoteFretboardX(scrape, scrape.fret, metrics, false);
    const auto alpha_at = [&](const double seconds) {
        return highwaySlideStateAt(scrape, base_x, metrics, false, seconds).alpha;
    };

    // The run spans onset to last waypoint (1.0 to 3.0), so the turnaround at 2.0 sits halfway
    // down the ramp rather than at its bottom.
    CHECK_THAT(alpha_at(1.0), Catch::Matchers::WithinAbs(1.0, 1e-12));
    CHECK_THAT(alpha_at(2.0), Catch::Matchers::WithinAbs(0.625, 1e-12));
    CHECK_THAT(alpha_at(3.0), Catch::Matchers::WithinAbs(g_unpitched_slide_end_alpha, 1e-12));

    // Monotone the whole way: no leg boundary lifts it.
    CHECK(alpha_at(1.5) > alpha_at(2.0));
    CHECK(alpha_at(2.0) > alpha_at(2.5));
}

// The density policy every glide-following mark subdivides by. Bounded at both ends so a
// sub-fret wiggle still reads as a curve and a full-neck scrape cannot tessellate past a batch
// budget, and monotone in between so a longer travel never draws with fewer slices.
TEST_CASE("Glide slice count is bounded, four per fret, and monotone", "[ui][highway]")
{
    // The floor holds for anything under one and a half frets of travel.
    CHECK(highwayGlideSliceCount(0.0) == g_glide_slice_min);
    CHECK(highwayGlideSliceCount(1.0) == g_glide_slice_min);

    // Four per fret past that.
    CHECK(highwayGlideSliceCount(2.0) == 8);
    CHECK(highwayGlideSliceCount(3.0) == 12);

    // The ceiling holds from sixteen frets of travel on, a scrape's whole-neck leg included.
    CHECK(highwayGlideSliceCount(16.0) == g_glide_slice_max);
    CHECK(highwayGlideSliceCount(48.0) == g_glide_slice_max);

    int previous = highwayGlideSliceCount(0.0);
    for (int step = 1; step <= 40; ++step)
    {
        const int count = highwayGlideSliceCount(static_cast<double>(step) / 2.0);
        CHECK(count >= previous);
        previous = count;
    }
}

} // namespace rock_hero::common::ui
