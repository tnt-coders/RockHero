#include "highway/highway_slide_path.h"

#include <array>
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
// below adds the keyframes it needs, so the shared part stays the note the board would draw.
[[nodiscard]] common::core::NoteViewState frettedNote()
{
    common::core::NoteViewState note;
    note.string = 1;
    note.fret = 5;
    note.start_seconds = 1.0;
    note.ring_end_seconds = 4.0;
    note.ink_end_seconds = 4.0;
    return note;
}

// One glide keyframe, pitched. A keyframe is unpitched exactly when it is the note's SLIDE-OUT —
// the slide-out terminal, which lives in the same list as its last entry and which
// `slideOutKeyframe` below states; a scrape's turnarounds are pitched stops like any other. The
// authored offset is left unstated — these fixtures resolve no tempo map, and only the editor's
// selection reads it.
[[nodiscard]] common::core::KeyframeViewState keyframe(const double seconds, const int fret)
{
    return common::core::KeyframeViewState{
        .seconds = seconds,
        .fret = fret,
        .offset = common::core::Fraction{},
        .slide_out = false,
    };
}

// The slide-out terminal: the note's last keyframe, sitting at the ring's end by definition.
[[nodiscard]] common::core::KeyframeViewState slideOutKeyframe(const double seconds, const int fret)
{
    return common::core::KeyframeViewState{
        .seconds = seconds,
        .fret = fret,
        .offset = common::core::Fraction{},
        .slide_out = true,
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

// The two ends of a glide segment: nothing has moved at the onset, and the offset at a keyframe's
// own time is exactly that keyframe's anchor. A mark walking this path therefore starts under its
// head and lands on the fret the chart says the gesture arrives at.
TEST_CASE("Glide holds the onset anchor at the onset and the target at a keyframe", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.slides = {keyframe(2.0, 9)};
    const double base_x = highwayNoteFretboardX(note, note.fret, metrics, false);

    const HighwaySlideState at_onset =
        highwaySlideStateAt(note, base_x, metrics, false, note.start_seconds);
    CHECK_THAT(at_onset.x_offset, Catch::Matchers::WithinULP(0.0, 0));
    CHECK_THAT(at_onset.alpha, Catch::Matchers::WithinULP(1.0, 0));

    const double target_x = highwayNoteFretboardX(note, 9, metrics, false);
    const HighwaySlideState at_keyframe = highwaySlideStateAt(note, base_x, metrics, false, 2.0);
    CHECK_THAT(base_x + at_keyframe.x_offset, Catch::Matchers::WithinAbs(target_x, 1e-12));
}

// Mid-segment the path is the EASED interpolation, in the family the arriving keyframe names —
// the same weights the tail's own centerline and the tapping hand's light travel by. A pitched
// glide eases symmetrically where an unpitched release leaves early, so the two are measurably
// apart at the same instant.
TEST_CASE("Mid-glide the path is the eased weight, pitched and unpitched apart", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState pitched = frettedNote();
    pitched.slides = {keyframe(2.0, 9)};
    // The unpitched arm is a slide-out terminal, which lands at the ring's end by definition —
    // so the ring is shortened to the instant the pitched arm's keyframe arrives at, and the two
    // segments span exactly the same time.
    common::core::NoteViewState unpitched = frettedNote();
    unpitched.ring_end_seconds = 2.0;
    unpitched.ink_end_seconds = 2.0;
    unpitched.slides = {slideOutKeyframe(2.0, 9)};

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

// Past the last keyframe the glide HOLDS its target, which is what "continue straight along the
// fret it stopped on" means: a gesture that has stopped travelling does not drift, and anything
// drawn past the last keyframe reads the fret it stopped on.
TEST_CASE("Past the last keyframe the glide holds its final target", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.slides = {keyframe(2.0, 9), keyframe(3.0, 12)};
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

// A tail whose ink stops short of the ring still draws the leg its ink end cuts on that leg's TRUE
// path: the keyframe past the ink end is not drawn, but the travel toward it is, so the drawn part
// of the leg is neither frozen at the onset nor bent onto a shortened glide.
TEST_CASE("A leg the ink end cuts is drawn on its true path", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.ink_end_seconds = 2.5;
    note.slides = {keyframe(3.0, 9)};
    REQUIRE_FALSE(common::core::keyframeDrawn(note.slides[0], note.ink_end_seconds));
    const double base_x = highwayNoteFretboardX(note, note.fret, metrics, false);
    const double travel = highwayNoteFretboardX(note, 9, metrics, false) - base_x;

    // Halfway along the stored leg (1.0 to 3.0), inside the drawn extent.
    CHECK_THAT(
        highwaySlideStateAt(note, base_x, metrics, false, 2.0).x_offset,
        Catch::Matchers::WithinAbs(
            travel * common::core::highwaySlideEaseWeight(0.5, false), 1e-12));
}

// A harmonic's node RIDES its stop: fret spacing is logarithmic, so the node keeps a constant
// offset in fret units above whatever the glide has travelled to. One rule places the onset and
// every station of the glide, which is why the anchor takes the stop as a parameter.
TEST_CASE("A harmonic's node rides its stop through a glide", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.harmonic_node = 7.2;
    note.slides = {keyframe(2.0, 9)};
    const double base_x = highwayNoteFretboardX(note, note.fret, metrics, false);

    // Four frets of travel move the node four fret units up, to 11.2.
    const HighwaySlideState arrived = highwaySlideStateAt(note, base_x, metrics, false, 2.0);
    CHECK_THAT(
        base_x + arrived.x_offset,
        Catch::Matchers::WithinAbs(common::core::highwayFretLineX(11.2, metrics, false), 1e-12));
}

// A scrape's dim spans its WHOLE path, not each leg: its chained legs are one continuous release,
// so the alpha must never snap back to full where the travel reverses. Only the geometry restarts
// per leg.
TEST_CASE("A scrape's dim ramps across its whole path", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    // A real scrape: a turnaround at 2.0, then the required terminal where the pick lifts at 3.0.
    // The turnaround is a pitched stop, and the dim still spans both legs, read off the attack.
    common::core::NoteViewState scrape = frettedNote();
    scrape.attack = common::core::NoteAttack::PickSlide;
    scrape.ring_end_seconds = 3.0;
    scrape.ink_end_seconds = 3.0;
    scrape.slides = {keyframe(2.0, 10), slideOutKeyframe(3.0, 3)};
    const double base_x = highwayNoteFretboardX(scrape, scrape.fret, metrics, false);
    const auto alpha_at = [&](const double seconds) {
        return highwaySlideStateAt(scrape, base_x, metrics, false, seconds).alpha;
    };

    // The dim spans onset to last keyframe (1.0 to 3.0), so the turnaround at 2.0 sits halfway
    // down the ramp rather than at its bottom.
    CHECK_THAT(alpha_at(1.0), Catch::Matchers::WithinAbs(1.0, 1e-12));
    CHECK_THAT(alpha_at(2.0), Catch::Matchers::WithinAbs(0.625, 1e-12));
    CHECK_THAT(alpha_at(3.0), Catch::Matchers::WithinAbs(g_unpitched_slide_end_alpha, 1e-12));

    // Monotone the whole way: no leg boundary lifts it, and just past the turnaround it carries
    // on from where it stood rather than snapping back to full.
    CHECK(alpha_at(1.5) > alpha_at(2.0));
    CHECK(alpha_at(2.0) > alpha_at(2.5));
    CHECK_THAT(alpha_at(2.0 + 1e-9), Catch::Matchers::WithinAbs(0.625, 1e-6));
}

// A scrape's turnarounds are places the pick reaches and turns from, so each interior leg rides
// the PITCHED curve and arrives tangentially — the rail's slope falls to zero into the stop, where
// the release curve would still be moving at full speed and corner there. Only the terminal leg,
// where the pick lifts toward a fret it never reaches, keeps the release curve.
TEST_CASE("A scrape arrives tangentially at every turnaround", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    // Down from 12 to 3, up to 10, then the terminal down toward 2: two turnarounds, one leg a
    // second each.
    common::core::NoteViewState scrape = frettedNote();
    scrape.fret = 12;
    scrape.attack = common::core::NoteAttack::PickSlide;
    scrape.slides = {keyframe(2.0, 3), keyframe(3.0, 10), slideOutKeyframe(4.0, 2)};
    const double base_x = highwayNoteFretboardX(scrape, scrape.fret, metrics, false);
    const auto rail_x = [&](const double seconds) {
        return base_x + highwaySlideStateAt(scrape, base_x, metrics, false, seconds).x_offset;
    };
    const auto stop_x = [&](const int fret) {
        return highwayNoteFretboardX(scrape, fret, metrics, false);
    };

    struct Leg
    {
        double start_seconds;
        int from_fret;
        int to_fret;
        bool unpitched;
    };
    const std::array<Leg, 3> legs{
        Leg{.start_seconds = 1.0, .from_fret = 12, .to_fret = 3, .unpitched = false},
        Leg{.start_seconds = 2.0, .from_fret = 3, .to_fret = 10, .unpitched = false},
        Leg{.start_seconds = 3.0, .from_fret = 10, .to_fret = 2, .unpitched = true},
    };
    for (const Leg& leg : legs)
    {
        const double from_x = stop_x(leg.from_fret);
        const double travel = stop_x(leg.to_fret) - from_x;
        for (const double progress : {0.25, 0.5, 0.9, 0.95, 0.99})
        {
            CHECK_THAT(
                rail_x(leg.start_seconds + progress),
                Catch::Matchers::WithinAbs(
                    from_x +
                        (travel * common::core::highwaySlideEaseWeight(progress, leg.unpitched)),
                    1e-9));
        }
        // The stop itself is reached exactly, whichever curve led there.
        CHECK_THAT(
            rail_x(leg.start_seconds + 1.0), Catch::Matchers::WithinAbs(from_x + travel, 1e-9));
    }

    // The slope into each turnaround, per unit of the leg's travel, falls toward zero as the
    // stop nears; the release curve over the same last stretch would still be moving at nearly
    // its full pi / 2.
    for (const Leg& leg : {legs[0], legs[1]})
    {
        const double arrive = leg.start_seconds + 1.0;
        const double travel = stop_x(leg.to_fret) - stop_x(leg.from_fret);
        const auto slope_before = [&](const double step) {
            return (rail_x(arrive) - rail_x(arrive - step)) / (step * travel);
        };
        const auto release_slope_before = [](const double step) {
            return (1.0 - common::core::highwaySlideEaseWeight(1.0 - step, true)) / step;
        };
        CHECK(slope_before(0.1) > slope_before(0.05));
        CHECK(slope_before(0.05) > slope_before(0.01));
        CHECK(slope_before(0.01) < 0.05);
        CHECK(release_slope_before(0.01) > 1.5);
    }
}

// On any note other than a scrape, the dim is the terminal slide-out's own leg: a pitched glide
// before it keeps the note's full brightness, and only the leg into the terminal fades.
TEST_CASE("A fretted glide dims only across its slide-out leg", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    common::core::NoteViewState note = frettedNote();
    note.ring_end_seconds = 3.0;
    note.ink_end_seconds = 3.0;
    note.slides = {keyframe(2.0, 9), slideOutKeyframe(3.0, 12)};
    const double base_x = highwayNoteFretboardX(note, note.fret, metrics, false);
    const auto alpha_at = [&](const double seconds) {
        return highwaySlideStateAt(note, base_x, metrics, false, seconds).alpha;
    };

    for (const double seconds : {1.0, 1.5, 1.99, 2.0})
    {
        CHECK_THAT(alpha_at(seconds), Catch::Matchers::WithinAbs(1.0, 1e-12));
    }
    CHECK_THAT(
        alpha_at(2.5),
        Catch::Matchers::WithinAbs(1.0 + ((g_unpitched_slide_end_alpha - 1.0) * 0.5), 1e-12));
    CHECK_THAT(alpha_at(3.0), Catch::Matchers::WithinAbs(g_unpitched_slide_end_alpha, 1e-12));
    CHECK_THAT(alpha_at(5.0), Catch::Matchers::WithinAbs(g_unpitched_slide_end_alpha, 1e-12));
}

} // namespace rock_hero::common::ui
