#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdlib>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// Coverage summed across one row of a window, in pixels. For an opaque silhouette over a
// transparent lane this is the row's true chord to sub-pixel accuracy, which counting pixels past
// a threshold cannot give. Coverage is alpha/255, the same definition the plectrum outline was
// measured under in the note atlas.
[[nodiscard]] double rowCoverage(const juce::Image& image, int y, int x_from, int x_to)
{
    double total = 0.0;
    for (int x = x_from; x <= x_to; ++x)
    {
        total += static_cast<double>(image.getPixelAt(x, y).getAlpha()) / 255.0;
    }
    return total;
}

// Coverage summed down one column of a window, in pixels.
[[nodiscard]] double columnCoverage(const juce::Image& image, int x, int y_from, int y_to)
{
    double total = 0.0;
    for (int y = y_from; y <= y_to; ++y)
    {
        total += static_cast<double>(image.getPixelAt(x, y).getAlpha()) / 255.0;
    }
    return total;
}

// True for pure white ink: the fret number and the full-mute X are the only things painted it, and
// no string color, head layer or chip surface reaches it, so this identifies them without the test
// having to know which lane color it is looking at.
[[nodiscard]] bool isWhiteInk(juce::Colour color)
{
    return color.getAlpha() >= 250 && color.getRed() >= 250 && color.getGreen() >= 250 &&
           color.getBlue() >= 250;
}

// Half-width of the head's digit window. Narrower than the beside-head chip's own clearance from
// the axis, so within it only the fret number can be white.
constexpr int g_digit_window = 4;

// Topmost row carrying digit ink inside that window.
[[nodiscard]] int topDigitInkRow(const juce::Image& image, int center_x, int center_y)
{
    for (int y = center_y - 20; y <= center_y + 20; ++y)
    {
        for (int x = center_x - g_digit_window; x <= center_x + g_digit_window; ++x)
        {
            if (isWhiteInk(image.getPixelAt(x, y)))
            {
                return y;
            }
        }
    }
    return 0;
}

// Fills the hand-built state's display hold ends and returns the visibility index over them. Every
// note in this file holds exactly its own sustain — none is a sustainless member of a strum under a
// hand-shape span, the one case the projection extends — so the two travel together here rather
// than being spelled out per fixture, which is the paint core's precondition.
[[nodiscard]] std::vector<double> resolveHoldEnds(common::core::TabViewState& state)
{
    state.display_hold_ends.clear();
    state.display_hold_ends.reserve(state.notes.size());
    for (const common::core::TabNoteView& note : state.notes)
    {
        state.display_hold_ends.push_back(note.end_seconds);
    }
    return common::core::makeSustainPrefixMax(state.display_hold_ends);
}

// The 400x240 six-lane band every case in this file paints into: 20 px/s across 20 seconds, so a
// second is twenty columns and 2.0s lands at x = 40.
[[nodiscard]] TabLaneMetrics referenceMetrics(int string_count)
{
    return makeTabLaneMetrics(
        juce::Rectangle<int>{0, 0, 400, 240},
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        common::core::displayedStringCount(string_count, 0),
        string_count,
        TabLaneStyle{});
}

// The largest per-channel difference anywhere between two renders: zero means the two are the same
// picture, which is the only way to state "draws exactly what the plain pick draws" without
// depending on where a mark's geometry happens to land.
[[nodiscard]] int worstPixelDelta(const juce::Image& lhs, const juce::Image& rhs)
{
    int worst = 0;
    for (int x = 0; x < lhs.getWidth(); ++x)
    {
        for (int y = 0; y < lhs.getHeight(); ++y)
        {
            const juce::Colour from_lhs = lhs.getPixelAt(x, y);
            const juce::Colour from_rhs = rhs.getPixelAt(x, y);
            worst = std::max(
                worst,
                std::max(
                    {std::abs(from_lhs.getAlpha() - from_rhs.getAlpha()),
                     std::abs(from_lhs.getRed() - from_rhs.getRed()),
                     std::abs(from_lhs.getGreen() - from_rhs.getGreen()),
                     std::abs(from_lhs.getBlue() - from_rhs.getBlue())}));
        }
    }
    return worst;
}

} // namespace

// An `Unjustified` claim draws exactly what the plain pick beside it draws: nothing. The whole
// no-indicator ruling rests on that identity — mid-burst a broken claim and a true pick are
// pixel-identical by design — so it is checked as an image identity rather than by probing where
// the triangle would have been, which is what makes it kill the mutation the branch invites:
// drawing the hammer for any stored `Legato` regardless of the resolution.
TEST_CASE("Tab paint core draws an unjustified claim as a plain pick", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const auto painted = [](const common::core::NoteAttack attack,
                            const common::core::LegatoMotion motion) {
        common::core::TabViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::TabNoteView{
                .start_seconds = 5.0,
                .end_seconds = 9.0,
                .string = 3,
                .fret = 7,
                .attack = attack,
                .legato = motion,
                .bend = {},
                .slides = {},
            },
        };
        const std::vector<double> prefix_max = resolveHoldEnds(state);
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(graphics, referenceMetrics(state.string_count), state, prefix_max);
        return image;
    };

    const juce::Image pick =
        painted(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    const juce::Image broken_claim =
        painted(common::core::NoteAttack::Legato, common::core::LegatoMotion::Unjustified);
    const juce::Image hammer =
        painted(common::core::NoteAttack::Legato, common::core::LegatoMotion::Hammer);
    const juce::Image pull =
        painted(common::core::NoteAttack::Legato, common::core::LegatoMotion::Pull);

    // The identity itself.
    CHECK(worstPixelDelta(pick, broken_claim) == 0);
    // And the mark really is drawn when the claim resolves, so the identity above cannot be passing
    // because nothing ever draws a triangle. The two directions are distinct pictures too — one
    // upright, one flipped — which is what keeps a "resolved means hammer" mutation from passing.
    CHECK(worstPixelDelta(pick, hammer) > 0);
    CHECK(worstPixelDelta(pick, pull) > 0);
    CHECK(worstPixelDelta(hammer, pull) > 0);
}

// An accent reaches the TAIL, not only the head. An accent mark states a louder DYNAMIC as well as
// a stronger attack, and a plucked string's whole ring scales with how hard it was struck — it
// rings as A * exp(-lambda * t), where picking harder raises A while lambda is fixed by the
// damping, so the note is louder at every instant it sounds rather than only at its onset. The
// quiet end of this axis already said so here by leaning the entire ink set, ribbon included, so a
// head-only accent left one axis saying two different things at its two ends.
//
// And it rides the RAILS with NO END CAP. The tail draws no cap at either end by the 2026-08-16
// ruling — a cap boxes in whatever technique mark reaches the tip — so a halo wrapping the tip
// would restore that cap in light and box the mark in exactly the same way. Past the tail's end
// the two renders must therefore be the SAME PICTURE, which is the half that the obvious
// implementation (grow the tail's outline, stroke it) silently fails while still lighting the
// rails correctly.
TEST_CASE("Tab paint core reaches an accent along the tail without capping it", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const auto painted = [](const common::core::NoteEmphasis emphasis) {
        common::core::TabViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::TabNoteView{
                .start_seconds = 5.0,
                .end_seconds = 9.0,
                .string = 3,
                .fret = 7,
                .emphasis = emphasis,
                .bend = {},
                .slides = {},
            },
        };
        const std::vector<double> prefix_max = resolveHoldEnds(state);
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(graphics, referenceMetrics(state.string_count), state, prefix_max);
        return image;
    };

    const juce::Image plain = painted(common::core::NoteEmphasis::Normal);
    const juce::Image accented = painted(common::core::NoteEmphasis::Accent);

    const common::core::TabNoteView probe{
        .start_seconds = 5.0,
        .end_seconds = 9.0,
        .string = 3,
        .fret = 7,
        .bend = {},
        .slides = {},
    };
    const TabNoteLayout layout = tabNoteLayout(referenceMetrics(6), probe, probe.end_seconds);

    const auto worstInBand =
        [&plain, &accented](const int x_from, const int x_to, const int y_from, const int y_to) {
            int worst = 0;
            for (int x = x_from; x <= x_to; ++x)
            {
                for (int y = y_from; y <= y_to; ++y)
                {
                    const juce::Colour from_plain = plain.getPixelAt(x, y);
                    const juce::Colour from_accented = accented.getPixelAt(x, y);
                    worst = std::max(
                        worst,
                        std::max(
                            {std::abs(from_plain.getAlpha() - from_accented.getAlpha()),
                             std::abs(from_plain.getRed() - from_accented.getRed()),
                             std::abs(from_plain.getGreen() - from_accented.getGreen()),
                             std::abs(from_plain.getBlue() - from_accented.getBlue())}));
                }
            }
            return worst;
        };

    const int tail_end = juce::roundToInt(layout.tail.x + layout.tail.width);
    const int tail_top = juce::roundToInt(layout.tail.y);
    const int tail_bottom = juce::roundToInt(layout.tail.y + layout.tail.height);
    // Sampled well along the tail, clear of the head's own halo, so this cannot pass on the head
    // glow that was already there.
    const int mid_from = juce::roundToInt(layout.tail.x + (layout.tail.width * 0.6f));

    // The halo is present on BOTH rails.
    CHECK(worstInBand(mid_from, mid_from + 4, tail_top - 4, tail_top - 1) > 0);
    CHECK(worstInBand(mid_from, mid_from + 4, tail_bottom + 1, tail_bottom + 4) > 0);
    // And it stops with them: nothing past the tail's end, on any row the halo occupies.
    CHECK(worstInBand(tail_end + 2, tail_end + 12, tail_top - 6, tail_bottom + 6) == 0);
}

// The stated left-hand tap wears its own charting mark — the tap letter in the fretting hand's
// LIGHT polarity — never the merged triangle and never the picking hand's dark plate. Three
// identities kill the three ways the mark can silently regress: drawing nothing, folding back
// into the resolved-motion branch, and drawing the right-hand tap's polarity (a dark T is the
// OTHER hand's statement — fill polarity is the hand signature).
TEST_CASE("Tab paint core draws a left-hand tap as the light tap plate", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const auto painted = [](const common::core::NoteAttack attack,
                            const common::core::LegatoMotion motion) {
        common::core::TabViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::TabNoteView{
                .start_seconds = 5.0,
                .end_seconds = 9.0,
                .string = 3,
                .fret = 7,
                .attack = attack,
                .legato = motion,
                .bend = {},
                .slides = {},
            },
        };
        const std::vector<double> prefix_max = resolveHoldEnds(state);
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(graphics, referenceMetrics(state.string_count), state, prefix_max);
        return image;
    };

    // A left-hand tap always resolves to the hammer motion, so the merged-branch mutation would
    // paint it exactly like the resolved hammer-on below.
    const juce::Image left_tap =
        painted(common::core::NoteAttack::LeftTap, common::core::LegatoMotion::Hammer);
    const juce::Image bare_pick =
        painted(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    const juce::Image hammer =
        painted(common::core::NoteAttack::Legato, common::core::LegatoMotion::Hammer);
    const juce::Image right_tap =
        painted(common::core::NoteAttack::Tap, common::core::LegatoMotion::Unjustified);

    // A mark exists at all; it is not the triangle; it is not the dark plate.
    CHECK(worstPixelDelta(left_tap, bare_pick) > 0);
    CHECK(worstPixelDelta(left_tap, hammer) > 0);
    CHECK(worstPixelDelta(left_tap, right_tap) > 0);
}

// Every tail the lane draws is drawn to the note's DISPLAY hold end, teeth and sine included, and
// the hit-test rectangle stops exactly where that ink does. A span-held strum member stores no
// sustain at all, so a tail keyed off `end_seconds` would draw nothing here and a hit rectangle
// keyed off it would leave the drawn ribbon dead to the pointer.
TEST_CASE("Tab paint core draws tails to the display hold end", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::TabViewState state;
    state.string_count = 6;
    // Three sustainless notes at 2.0s — plain, tremolo, vibrato — each span-held to 8.0s (x = 160).
    const auto member = [](int string, bool tremolo, bool vibrato) {
        return common::core::TabNoteView{
            .start_seconds = 2.0,
            .end_seconds = 2.0,
            .string = string,
            .fret = 7,
            .vibrato = vibrato,
            .tremolo = tremolo,
            .bend = {},
            .slides = {},
        };
    };
    state.notes = {member(2, false, false), member(3, true, false), member(4, false, true)};
    state.display_hold_ends = {8.0, 8.0, 8.0};

    const TabLaneMetrics metrics = referenceMetrics(state.string_count);
    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    paintTabLane(
        graphics, metrics, state, common::core::makeSustainPrefixMax(state.display_hold_ends));

    // The empty lane for reference: string lines run the full width, so "where the tail ends" must
    // be measured as a difference from the furniture rather than as raw coverage.
    const juce::Image lanes_only{juce::SoftwareImageType{}.create(
        juce::Image::ARGB, 400, 240, true)};
    {
        common::core::TabViewState bare;
        bare.string_count = state.string_count;
        juce::Graphics bare_graphics{lanes_only};
        paintTabLane(bare_graphics, metrics, bare, {});
    }

    // True where this lane's band carries something the empty lane does not.
    const auto differs = [&](const int string, const int x) {
        const int center_y = juce::roundToInt(metrics.laneY(string));
        for (int y = center_y - 18; y <= center_y + 18; ++y)
        {
            if (image.getPixelAt(x, y) != lanes_only.getPixelAt(x, y))
            {
                return true;
            }
        }
        return false;
    };
    const auto last_inked_column = [&](const int string) {
        int last = 0;
        for (int x = 0; x < 400; ++x)
        {
            if (differs(string, x))
            {
                last = x;
            }
        }
        return last;
    };

    for (std::size_t index = 0; index < state.notes.size(); ++index)
    {
        const common::core::TabNoteView& note = state.notes[index];
        CAPTURE(note.string);
        // Ink well past the stored end (x = 40) proves the ribbon was drawn from the hold end, and
        // the tremolo teeth and the vibrato sine ride the same length rather than their own.
        CHECK(differs(note.string, 120));
        // And it stops there: nothing is drawn past the hold end on any of the three.
        const int last_column = last_inked_column(note.string);
        CHECK(last_column <= 161);
        // The hit rectangle agrees with the drawn ink to the pixel, which is the whole point of the
        // manifest taking the same hold end the paint pass does.
        const TabNoteLayout layout = tabNoteLayout(metrics, note, state.display_hold_ends[index]);
        CHECK(std::abs(last_column - juce::roundToInt(layout.tail.x + layout.tail.width)) <= 1);
    }
}

// Techniques, shape spans, and fret-hand positions all draw without touching empty lanes.
// Moved from the editor's TabView suite when the paint core was extracted (plan 30 Phase 2);
// every probe color is unchanged, so the core's output is pinned to the editor lane's shipped
// pixels.
TEST_CASE("Tab paint core draws techniques, shapes, and fret-hand positions", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::TabViewState state;
    state.string_count = 6;
    state.notes = {
        common::core::TabNoteView{
            .start_seconds = 2.0,
            .end_seconds = 8.0,
            .string = 1,
            .fret = 5,
            // A stored claim plus the motion it resolved to: the mark comes from the resolution, so
            // the projection's answer is what the paint core has to be handed.
            .attack = common::core::NoteAttack::Legato,
            .legato = common::core::LegatoMotion::Hammer,
            .palm_mute = true,
            .vibrato = true,
            .emphasis = common::core::NoteEmphasis::Accent,
            .bend = {common::core::TabBendPointView{.seconds = 4.0, .semitones = 2.0}},
            .slides = {common::core::TabSlideView{.seconds = 7.0, .fret = 9, .unpitched = false}},
        },
        common::core::TabNoteView{
            .start_seconds = 3.0,
            .end_seconds = 6.0,
            .string = 2,
            .fret = 12,
            .harmonic_node = 12.0,
            .tremolo = true,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 3.0,
            .end_seconds = 3.0,
            .string = 3,
            .fret = 7,
            // A pinch carries its node like every harmonic, but 24.0 sits past the neck where the
            // thumb grazes, so the head still labels the fret (7) rather than the node.
            .attack = common::core::NoteAttack::Pinch,
            .dead = true,
            .harmonic_node = 24.0,
            .bend = {},
            .slides = {},
        },
    };
    state.shapes = {
        common::core::TabShapeView{
            .start_seconds = 2.0,
            .end_seconds = 6.0,
            .name = "A5",
            .arpeggio = false,
            .arpeggio_notes = {},
        },
        common::core::TabShapeView{
            .start_seconds = 10.0,
            .end_seconds = 12.0,
            .name = "Dm",
            .arpeggio = true,
            .arpeggio_notes = {
                common::core::TabArpeggioNoteView{.string = 3, .fret = 7},
                common::core::TabArpeggioNoteView{.string = 5, .fret = 8},
            },
        },
    };
    state.fret_hand_positions = {
        common::core::TabFhpView{.seconds = 2.0, .fret = 5, .width = 4},
        // Wider than the standard four-fret hand: the marker spells out the inclusive range.
        common::core::TabFhpView{.seconds = 14.0, .fret = 3, .width = 5},
    };

    const juce::Rectangle<int> bounds{0, 0, 400, 240};
    const common::core::TimeRange visible_timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    const TabLaneMetrics metrics = makeTabLaneMetrics(
        bounds,
        visible_timeline,
        common::core::displayedStringCount(state.string_count, 0),
        state.string_count);

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    const std::vector<double> prefix_max = resolveHoldEnds(state);
    paintTabLane(graphics, metrics, state, prefix_max);

    // The strummed A5 span rails the lane's top and bottom edges in the brightened hand-shape
    // blue (base x1.5) inside its range and not outside it, and does not tint the lane
    // interior. The probe column sits at 4.5s, inside the span.
    CHECK(image.getPixelAt(90, 1) == juce::Colour{0xff4982fa});
    CHECK(image.getPixelAt(90, 238) == juce::Colour{0xff4982fa});
    CHECK(image.getPixelAt(90, 5).getARGB() == 0);
    CHECK(image.getPixelAt(150, 1).getARGB() == 0);
    CHECK(image.getPixelAt(150, 238).getARGB() == 0);

    // The arpeggio span's rails are the purple at its own gentler brightness (base x1.3, user
    // tuned darker than the chord blue's x1.5).
    CHECK(image.getPixelAt(220, 1) == juce::Colour{0xffac73ed});
    CHECK(image.getPixelAt(220, 238) == juce::Colour{0xffac73ed});

    // Shape names draw in the host's name-chip band, not in the lane, so nothing but the rails
    // and the FHP marker touches the lane's top edge here.

    // Onsets carry no vertical bars: the columns between lanes at the strummed onset (3.0s)
    // and the arpeggio start (10.0s) stay empty.
    CHECK(image.getPixelAt(60, 110).getARGB() == 0);
    CHECK(image.getPixelAt(200, 110).getARGB() == 0);

    // The arpeggio start marks every posture string with square brackets hugging the head
    // ring — probed on the right bracket's vertical (x ~214.7 from the lane center at 200),
    // clear of string lines, serifs, and text: string 5 (lane center y = 60) and string 3
    // (y = 140) both wear them.
    CHECK(image.getPixelAt(214, 55).getARGB() != 0);
    CHECK(image.getPixelAt(214, 135).getARGB() != 0);

    // Nothing sounds on either posture string at the span start, so each states its held fret in
    // the bracket's CENTRE — the slot a fret number belongs in.
    const auto centred_digit_ink = [&image](int center_y) {
        for (int x = 194; x <= 206; ++x)
        {
            for (int y = center_y - 6; y <= center_y + 6; ++y)
            {
                if (image.getPixelAt(x, y).getARGB() != 0)
                {
                    return true;
                }
            }
        }
        return false;
    };
    CHECK(centred_digit_ink(60));
    CHECK(centred_digit_ink(140));

    // And neither earns a ground, because no sustain is crossing either column here. A ground is
    // a filled band, so its corners would be opaque where a glyph's never are.
    CHECK(image.getPixelAt(195, 55).getARGB() == 0);
    CHECK(image.getPixelAt(204, 65).getARGB() == 0);

    // The string line hides across the bracket and resumes past it.
    CHECK(image.getPixelAt(208, 60).getARGB() == 0);
    CHECK(image.getPixelAt(230, 60).getARGB() != 0);

    // The tremolo strip stays clipped to its sustain: nothing straggles past the note end.
    // String 2 lane of six in 240px: center y = 180. Note ends at 6.0s → x = 120. The probe row
    // sits above the string line (which runs the full width) but inside the tremolo band.
    bool tremolo_inside = false;
    for (int x = 62; x < 118; ++x)
    {
        tremolo_inside = tremolo_inside || image.getPixelAt(x, 174).getARGB() != 0;
    }
    CHECK(tremolo_inside);
    for (int x = 123; x < 240; ++x)
    {
        CHECK(image.getPixelAt(x, 174).getARGB() == 0);
    }

    // The vibrato-and-slide note still anchors its head at the onset on the bottom lane.
    CHECK(image.getPixelAt(33, 220).getARGB() != 0);

    // The five-fret-wide FHP at 14.0s (x = 280) draws its "3-7" range marker box along the top
    // edge; the probe sits inside the box fill, left of the centered text.
    CHECK(image.getPixelAt(282, 7) == juce::Colour{0xff2a2f36});
}

// The case the posture rule exists for: a right-hand tap at the span start on a posture string,
// sounding a DIFFERENT fret from the held one. The tap keeps the bracket's centre — it is what
// rings — and the still-held posture moves to a side chip whose ground masks whatever technique
// rides the crossing sustain. A silent posture string in the same span keeps its digit centred
// with no chip at all, so the side slot is conditional, never a fixture.
TEST_CASE("Tab paint core displaces a tapped posture to a grounded side chip", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::TabViewState state;
    state.string_count = 6;
    state.notes = {
        // Rings across the span start on string 3 with vibrato, so its sine crosses the side
        // chip's column — the ink the chip's ground exists to mask.
        common::core::TabNoteView{
            .start_seconds = 7.0,
            .end_seconds = 13.0,
            .string = 3,
            .fret = 7,
            .vibrato = true,
            .bend = {},
            .slides = {},
        },
        // The tap: span-start onset on the same string at a fret the posture does not hold.
        common::core::TabNoteView{
            .start_seconds = 10.0,
            .end_seconds = 10.0,
            .string = 3,
            .fret = 12,
            .attack = common::core::NoteAttack::Tap,
            .bend = {},
            .slides = {},
        },
    };
    state.shapes = {
        common::core::TabShapeView{
            .start_seconds = 10.0,
            .end_seconds = 14.0,
            .name = "X",
            .arpeggio = true,
            .arpeggio_notes = {
                common::core::TabArpeggioNoteView{.string = 3, .fret = 7},
                common::core::TabArpeggioNoteView{.string = 5, .fret = 8},
            },
        },
    };

    const juce::Rectangle<int> bounds{0, 0, 400, 240};
    const common::core::TimeRange visible_timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    const TabLaneMetrics metrics = makeTabLaneMetrics(
        bounds,
        visible_timeline,
        common::core::displayedStringCount(state.string_count, 0),
        state.string_count);

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    const std::vector<double> prefix_max = resolveHoldEnds(state);
    paintTabLane(graphics, metrics, state, prefix_max);

    // Six lanes in 240px: string 3 renders at lane center y = 140, string 5 at y = 60. The span
    // start (10.0s) lands at x = 200; the bracket's closing bar ends at x = 216, so the side
    // chip's ground starts on column 216 and its digit sits from 217.
    const auto white_in = [&image](int left, int right, int top, int bottom) {
        for (int x = left; x <= right; ++x)
        {
            for (int y = top; y <= bottom; ++y)
            {
                const juce::Colour pixel = image.getPixelAt(x, y);
                if (pixel.getRed() >= 0xF0 && pixel.getGreen() >= 0xF0 && pixel.getBlue() >= 0xF0)
                {
                    return true;
                }
            }
        }
        return false;
    };

    // String 3: the posture "7" states in the side chip, in white, right of the closing bar.
    CHECK(white_in(217, 227, 135, 144));

    // The chip's ground is the tail's own fill, and it MASKS the sine. The vibrato path crosses
    // column 216 at row 144 with full coverage, so without the ground that pixel would carry the
    // sine's grey; with it, it is exactly the tail fill.
    const juce::Colour tail_fill{StringLaneStyle{metrics.baseColor(3).getARGB()}.linked_inner};
    CHECK(image.getPixelAt(216, 144) == tail_fill);

    // And the sine really does draw past the chip, so the masked pixel is a masked pixel rather
    // than a sine that never reached the column: some full-coverage stretch of the wave sits just
    // right of the chip.
    const auto sine_grey_in = [&image](int left, int right, int top, int bottom) {
        for (int x = left; x <= right; ++x)
        {
            for (int y = top; y <= bottom; ++y)
            {
                const juce::Colour pixel = image.getPixelAt(x, y);
                const int red = pixel.getRed();
                if (pixel.getAlpha() == 255 && red >= 0x98 &&
                    std::abs(red - pixel.getGreen()) <= 8 &&
                    std::abs(pixel.getGreen() - pixel.getBlue()) <= 8)
                {
                    return true;
                }
            }
        }
        return false;
    };
    CHECK(sine_grey_in(228, 234, 140, 148));

    // String 5 is silent at the span start, so its posture "8" states CENTRED in the bracket —
    // with no ground (centred digits never need one: technique marks clip against the bracket's
    // own columns, so the corner stays empty) and no side chip.
    CHECK(white_in(192, 208, 53, 67));
    CHECK(image.getPixelAt(192, 54).getARGB() == 0);
    CHECK_FALSE(white_in(218, 228, 54, 66));
}

// A fret-hand harmonic's head names its node, not its fret.
TEST_CASE("Tab paint core labels a harmonic head with its node", "[ui][tab-paint]")
{
    const auto head_text = [](const int fret,
                              const std::optional<double>
                                  node,
                              const common::core::NoteAttack attack) {
        common::core::TabNoteView note;
        note.string = 1;
        note.fret = fret;
        note.attack = attack;
        note.harmonic_node = node;
        return common::ui::tabNoteHeadText(note, note.fret);
    };
    constexpr auto pick = common::core::NoteAttack::Pick;

    // A plain note keeps showing its fret.
    CHECK(head_text(7, std::nullopt, pick) == "7");

    // Whole-numbered nodes drop the ".0" so the common positions stay as narrow as a fret number,
    // even when the node disagrees with the integer anchor in `fret`.
    CHECK(head_text(12, 12.0, pick) == "12");
    CHECK(head_text(0, 12.0, pick) == "12");
    CHECK(head_text(7, 7.0, pick) == "7");

    // Genuinely fractional nodes keep one decimal — the 6th and 7th partials, and the widest label
    // the head has to hold.
    CHECK(head_text(3, 3.2, pick) == "3.2");
    CHECK(head_text(2, 2.7, pick) == "2.7");
    CHECK(head_text(14, 14.7, pick) == "14.7");

    // Higher partials crowd toward the nut, below fret 1.
    CHECK(head_text(1, 1.1, pick) == "1.1");

    // A tap harmonic's damping finger lands ON the neck, so it labels the node like any other.
    CHECK(head_text(5, 17.0, common::core::NoteAttack::Tap) == "17");

    // A pinch keeps its FRET: its node is off the neck over the pickups, and 2D has no axis for it,
    // so labelling 24.0 here would name a fret the hand is nowhere near (25-Q5).
    CHECK(head_text(5, 24.0, common::core::NoteAttack::Pinch) == "5");
}

// A pick scrape's head is the plectrum silhouette measured off the note atlas's pick-slide cell,
// and it carries its identity by SHAPE: no borrowed full-mute X, no boxed fret number. Every probe
// here reads the RIGHT half of the head, because the beside-head chip occupies the left.
TEST_CASE("Tab paint core draws a pick scrape as a plectrum head", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::TabViewState state;
    state.string_count = 6;
    // Four zero-length notes on one lane, differing only in what should change the head. Zero
    // length keeps every head clean: drawNoteTail returns early, so no sustain ribbon reaches
    // the probes.
    state.notes = {
        common::core::TabNoteView{
            .start_seconds = 4.0,
            .end_seconds = 4.0,
            .string = 3,
            .fret = 5,
            .attack = common::core::NoteAttack::PickSlide,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 8.0,
            .end_seconds = 8.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .dead = true,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 16.0,
            .end_seconds = 16.0,
            .string = 3,
            .fret = 5,
            .harmonic_node = 5.0,
            .bend = {},
            .slides = {},
        },
        // The widest number the raise has to hold, on its own lane: two digits reach far enough
        // left to meet the chip that caps the raise, which one digit never does.
        common::core::TabNoteView{
            .start_seconds = 4.0,
            .end_seconds = 4.0,
            .string = 5,
            .fret = 12,
            .attack = common::core::NoteAttack::PickSlide,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 8.0,
            .end_seconds = 8.0,
            .string = 5,
            .fret = 12,
            .bend = {},
            .slides = {},
        },
    };

    const juce::Rectangle<int> bounds{0, 0, 400, 240};
    const common::core::TimeRange visible_timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    const TabLaneMetrics metrics = makeTabLaneMetrics(
        bounds,
        visible_timeline,
        common::core::displayedStringCount(state.string_count, 0),
        state.string_count);
    // Six lanes in 240 px put note_height at its 25 px ceiling, so the head is 26 px and every
    // onset lands on a whole pixel: 20 px per second puts the string-3 heads at x = 80, 160, 240
    // and 320, and the two-digit pair on string 5 at 80 and 160. Whole-pixel centers matter — the
    // silhouette is symmetric about a pixel BOUNDARY there, so a right-half probe is exactly half
    // the chord.
    REQUIRE_THAT(metrics.note_height, Catch::Matchers::WithinULP(25.0f, 0));
    REQUIRE(metrics.draw_text);

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    const std::vector<double> prefix_max = resolveHoldEnds(state);
    paintTabLane(graphics, metrics, state, prefix_max);

    constexpr int scrape_x = 80;
    constexpr int plain_x = 160;
    constexpr int mute_x = 240;
    constexpr int harmonic_x = 320;
    constexpr int lane_y = 140; // string 3 of six in 240 px

    // UPPER-HEAVY SILHOUETTE. The plectrum's widest row sits 3.94 px above the string line and it
    // tapers to a tip below, so its half-chord four rows up beats the one two rows down by about
    // 2.3 px (12.1 against 9.8). A disc cannot do that: its widest row IS the string line, so the
    // same difference comes out negative (12.4 against 12.9). This is the whole shape claim, and it
    // reads off the paths alone with no font involved.
    const double scrape_upper = rowCoverage(image, lane_y - 4, scrape_x, scrape_x + 15);
    const double scrape_lower = rowCoverage(image, lane_y + 2, scrape_x, scrape_x + 15);
    const double plain_upper = rowCoverage(image, lane_y - 4, plain_x, plain_x + 15);
    const double plain_lower = rowCoverage(image, lane_y + 2, plain_x, plain_x + 15);
    CHECK(scrape_upper - scrape_lower > 1.5);
    CHECK(plain_upper - plain_lower < 0.0);

    // The silhouette stands exactly as tall as the disc it replaces and 0.9395 as wide, so the
    // lane's vertical collision budget is untouched. Height is the center column's coverage (the
    // head is opaque, so the string line under it adds nothing); width is twice the widest sampled
    // half-chord, which lands a little under the true maximum because it is averaged over a whole
    // pixel row.
    //
    // The absolute figures dropped by 2 * border on 2026-08-19, when the head's dark outer backing
    // was dropped. That layer was the lane's own ground colour, so it never showed in the editor;
    // it DID paint here, because these cases render onto a transparent image where any opaque
    // layer counts as coverage. What the eye sees is unchanged — the bright ring was always the
    // head's visible edge, and it has not moved. The disc-versus-plectrum equality below is the
    // assertion that actually carries this case, and it is untouched by the drop.
    const double scrape_height = columnCoverage(image, scrape_x, lane_y - 20, lane_y + 20);
    const double plain_height = columnCoverage(image, plain_x, lane_y - 20, lane_y + 20);
    CHECK(scrape_height > 22.0);
    CHECK(scrape_height < 23.0);
    CHECK(std::abs(scrape_height - plain_height) < 0.5);
    const double scrape_aspect = 2.0 * scrape_upper / scrape_height;
    CHECK(scrape_aspect > 0.92);
    CHECK(scrape_aspect < 0.95);

    // NO BORROWED X. The full mute's lower arms cover the head's bottom corners; the plectrum has
    // tapered to a 3.4 px half-chord by that row, so those corners are empty for a scrape and white
    // for the mute that owns the mark. The taper is a narrowing head, not a missing one: two pixels
    // right of the axis, eight rows down, is still fully opaque (the outline is 5.2 px out there).
    CHECK(image.getPixelAt(scrape_x + 10, lane_y + 10).getAlpha() == 0);
    CHECK(image.getPixelAt(scrape_x - 10, lane_y + 10).getAlpha() == 0);
    CHECK(isWhiteInk(image.getPixelAt(mute_x + 10, lane_y + 10)));
    CHECK(isWhiteInk(image.getPixelAt(mute_x - 10, lane_y + 10)));
    CHECK(image.getPixelAt(scrape_x + 2, lane_y + 8).getAlpha() == 255);

    // NO BOXED NUMBER. A real mute fills a plate behind the digit; the same probe on the scrape
    // reads the head's own colored center instead. The full mute's plate takes its own X's light
    // fill (mutePlatePalette, 2026-08-18), so this probe now separates plate-from-head rather
    // than plate-from-X - the plate is deliberately invisible against the mark it centers.
    CHECK(isWhiteInk(image.getPixelAt(mute_x + 3, lane_y + 5)));
    CHECK(!isWhiteInk(image.getPixelAt(scrape_x + 3, lane_y + 5)));
    CHECK(image.getPixelAt(scrape_x + 3, lane_y + 5).getAlpha() == 255);

    // THE DIGIT RIDES 3 PX HIGHER, and only on the plectrum. The glyph is the same raster shifted
    // by 0.1154 * 26 = 3.0004 px, so the topmost inked row moves by exactly three; the disc and the
    // diamond must not move at all.
    const int scrape_digit_top = topDigitInkRow(image, scrape_x, lane_y);
    const int plain_digit_top = topDigitInkRow(image, plain_x, lane_y);
    const int harmonic_digit_top = topDigitInkRow(image, harmonic_x, lane_y);
    CHECK(plain_digit_top - scrape_digit_top == 3);
    CHECK(harmonic_digit_top == plain_digit_top);

    // The head's own solid center is what replaces the plate, so the raised digit has to sit well
    // inside it. Every inked pixel keeps a two-pixel margin from the near-black outer ring and from
    // the empty lane, which is what would fail first if the raise overshot the plectrum's broad
    // band or the silhouette were too narrow to hold a number unboxed. Checked for the two-digit
    // fret as well, on its own lane: that is the widest number the unboxed head has to hold.
    // The scan starts at the digit's own topmost ink row rather than a fixed reach above the
    // lane center: the beside-head chip sits above-left, and its rim is white ink too now that
    // the plate palette mirrors — a symmetric window would read the chip's rim as digit ink and
    // fail on the empty lane beyond it.
    const auto digit_ink_clear_of_rim =
        [&image](int center_x, int center_y, int digit_top, int window) {
            int found = 0;
            std::string offender;
            bool clear = true;
            for (int y = digit_top; y <= center_y + 20; ++y)
            {
                for (int x = center_x - window; x <= center_x + window; ++x)
                {
                    if (!isWhiteInk(image.getPixelAt(x, y)))
                    {
                        continue;
                    }
                    ++found;
                    for (int dy = -2; dy <= 2; ++dy)
                    {
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            const juce::Colour near_ink = image.getPixelAt(x + dx, y + dy);
                            // Head ink, and not the lane's ground. The bar was full opacity until
                            // 2026-08-19, which only held because the head's dark backing sat
                            // behind its ring and made the ring's ANTIALIASED outer edge composite
                            // to 255; with that layer dropped the same edge reports partial alpha
                            // (measured B996FF4F, the string's own green at 185) while looking
                            // identical in the editor, where it composites over the lane instead
                            // of over this case's transparent image. What the case is really
                            // asserting survives intact: the digit sits on the head's own coloured
                            // centre rather than on a plate or hanging off onto the lane, and a
                            // pixel outside the head still reads alpha 0 and fails.
                            const bool ok =
                                near_ink.getAlpha() > 0 && near_ink != juce::Colour{0xff101010};
                            if (!ok && offender.empty())
                            {
                                offender = "first offender at (" + std::to_string(x + dx) + "," +
                                           std::to_string(y + dy) +
                                           ") = " + near_ink.toDisplayString(true).toStdString() +
                                           "  from digit ink at (" + std::to_string(x) + "," +
                                           std::to_string(y) + ")";
                            }
                            clear = clear && ok;
                        }
                    }
                }
            }
            return std::tuple{found, clear, offender};
        };

    const auto [one_digit_ink, one_digit_clear, one_digit_offender] =
        digit_ink_clear_of_rim(scrape_x, lane_y, scrape_digit_top, g_digit_window);
    CHECK(one_digit_ink > 0);
    INFO(one_digit_offender);
    CHECK(one_digit_clear);

    // The two-digit scrape sits on string 5 (lane center y = 60). Its window stops short of the
    // chip's own letters so only the fret number answers.
    constexpr int wide_lane_y = 60;
    const int wide_scrape_digit_top = topDigitInkRow(image, scrape_x, wide_lane_y);
    const auto [two_digit_ink, two_digit_clear, two_digit_offender] =
        digit_ink_clear_of_rim(scrape_x, wide_lane_y, wide_scrape_digit_top, 6);
    CHECK(two_digit_ink > one_digit_ink);
    INFO(two_digit_offender);
    CHECK(two_digit_clear);
    // And it is raised by the same three pixels as the one-digit number.
    const int wide_plain_digit_top = topDigitInkRow(image, plain_x, wide_lane_y);
    CHECK(wide_plain_digit_top - wide_scrape_digit_top == 3);
}

// A scrape's tail is a PLAIN ribbon and every turnaround wears the note's own head.
//
// The teeth mean repeated attacks, so only tremolo wears them: a scrape is one continuous drag and
// the rules forbid tremolo picking it outright, so teeth there would assert a repetition it never
// performs. The division across the whole tail axis is that the head says what kind of attack and
// the tail says what happens over time, which leaves the scrape's noise to its plectrum head and
// its travel to the slide diagonals. Each turnaround then repeats that head, so a change of
// direction reads as one gesture continuing rather than a chain of disconnected diagonals — and it
// is where the traveled fret is stated. The unpitched terminal is not a turnaround and gets no
// head: nothing continues past it.
TEST_CASE("Tab paint core draws a scrape's tail plain and heads its turnarounds", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const juce::Rectangle<int> bounds{0, 0, 400, 240};
    const common::core::TimeRange visible_timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };

    // One sustained note on string 3, painted twice: as a scrape with two turnarounds and an
    // unpitched terminal, and as a plain tremolo of the same span. 20 px per second puts the onset
    // at x = 80, the turnarounds at 120 and 200, and the terminal at 240.
    const auto paint = [&](const bool scrape) {
        common::core::TabViewState state;
        state.string_count = 6;
        common::core::TabNoteView note{
            .start_seconds = 4.0,
            .end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
        };
        if (scrape)
        {
            note.attack = common::core::NoteAttack::PickSlide;
            note.slides = {
                common::core::TabSlideView{.seconds = 6.0, .fret = 9, .unpitched = true},
                common::core::TabSlideView{.seconds = 10.0, .fret = 3, .unpitched = true},
                common::core::TabSlideView{
                    .seconds = 12.0, .fret = 12, .unpitched = true, .linked = false
                },
            };
        }
        else
        {
            note.tremolo = true;
        }
        state.notes = {note};

        const TabLaneMetrics metrics = makeTabLaneMetrics(
            bounds,
            visible_timeline,
            common::core::displayedStringCount(state.string_count, 0),
            state.string_count);
        REQUIRE(metrics.draw_text);
        juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        const std::vector<double> prefix_max = resolveHoldEnds(state);
        paintTabLane(graphics, metrics, state, prefix_max);
        return image;
    };

    const juce::Image scraped = paint(true);
    const juce::Image tremoloed = paint(false);

    constexpr int lane_y = 140; // string 3 of six in 240 px
    // Columns well inside the tail and clear of every head: the onset and turnaround heads are
    // 26 px wide, so they reach 133 and 187 at the most.
    constexpr std::array<int, 4> open_columns{150, 160, 170, 180};

    // Topmost inked row of the tail in a column, which is the band's upper edge there.
    const auto top_row = [&lane_y](const juce::Image& image, const int x) {
        for (int y = lane_y - 20; y <= lane_y + 20; ++y)
        {
            if (image.getPixelAt(x, y).getAlpha() != 0)
            {
                return y;
            }
        }
        return 0;
    };

    // A PLAIN RIBBON HAS A STRAIGHT EDGE. The scrape's upper edge sits on the same row in every
    // sampled column; the tremolo band swings its whole strip, so its edge moves between them.
    // Both are drawn, so neither reading comes from an empty lane.
    int scrape_low = 999;
    int scrape_high = 0;
    int tremolo_low = 999;
    int tremolo_high = 0;
    for (const int x : open_columns)
    {
        scrape_low = std::min(scrape_low, top_row(scraped, x));
        scrape_high = std::max(scrape_high, top_row(scraped, x));
        tremolo_low = std::min(tremolo_low, top_row(tremoloed, x));
        tremolo_high = std::max(tremolo_high, top_row(tremoloed, x));
    }
    CHECK(scrape_high > 0);
    CHECK(scrape_high == scrape_low);
    CHECK(tremolo_high - tremolo_low >= 2);
    // And the ribbon stays inside the band the teeth swing through, so the plain tail is the
    // toothed one's core rather than a differently sized shape.
    CHECK(scrape_low >= tremolo_low);

    // A TURNAROUND WEARS THE NOTE'S OWN HEAD, row for row. A head is taller than the ribbon, so the
    // rows BELOW the ribbon hold nothing but head, which isolates the silhouette from the ribbon
    // the columns share. Measured against the onset's own head rather than re-deriving the
    // plectrum's shape: the scrape-head case above already pins that shape, and the ruling here is
    // precisely that a junction repeats it.
    int ribbon_bottom = 0;
    for (int y = lane_y + 20; y >= lane_y - 20; --y)
    {
        if (scraped.getPixelAt(160, y).getAlpha() != 0)
        {
            ribbon_bottom = y;
            break;
        }
    }
    REQUIRE(ribbon_bottom > lane_y);
    const auto below_ribbon_profile = [&](const int center_x) {
        std::vector<double> rows;
        for (int y = ribbon_bottom + 1; y <= lane_y + 20; ++y)
        {
            rows.push_back(rowCoverage(scraped, y, center_x - 15, center_x + 15));
        }
        return rows;
    };
    const std::vector<double> onset_profile = below_ribbon_profile(80);
    double onset_ink = 0.0;
    for (const double row : onset_profile)
    {
        onset_ink += row;
    }
    CHECK(onset_ink > 10.0);
    for (const int turnaround_x : {120, 200})
    {
        const std::vector<double> profile = below_ribbon_profile(turnaround_x);
        double worst = 0.0;
        for (std::size_t row = 0; row < profile.size(); ++row)
        {
            worst = std::max(worst, std::abs(profile[row] - onset_profile[row]));
        }
        CHECK(worst < 1.0);
        // And each states its own traveled fret, in the head's own raised digit position.
        CHECK(topDigitInkRow(scraped, turnaround_x, lane_y) > 0);
    }

    // THE TERMINAL IS NOT A TURNAROUND. Nothing continues past an unpitched release, so it draws no
    // head — only the chip naming where the string was let go — and the rows below the ribbon that
    // every turnaround fills stay empty at its column.
    int terminal_pixels = 0;
    for (int y = ribbon_bottom + 1; y <= lane_y + 20; ++y)
    {
        for (int x = 240 - 15; x <= 240 + 15; ++x)
        {
            terminal_pixels += scraped.getPixelAt(x, y).getAlpha() != 0 ? 1 : 0;
        }
    }
    CHECK(terminal_pixels == 0);
}

// The capo chip is the lane's only capo indication (absolute frets say nothing about the string
// floor), pinned to the bounds' top-left corner rather than the timeline, and absent without a
// capo.
TEST_CASE("Tab paint core pins a capo chip to the lane corner", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::TabViewState state;
    state.string_count = 6;
    state.capo = 2;

    const juce::Rectangle<int> bounds{0, 0, 400, 240};
    const common::core::TimeRange visible_timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    const TabLaneMetrics metrics = makeTabLaneMetrics(
        bounds,
        visible_timeline,
        common::core::displayedStringCount(state.string_count, 0),
        state.string_count);

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    const std::vector<double> prefix_max = resolveHoldEnds(state);
    paintTabLane(graphics, metrics, state, prefix_max);

    // The chip's box fills the FHP-chip chrome color behind its centered letters; the probe sits
    // inside the box near its left edge, clear of the text.
    CHECK(image.getPixelAt(4, 7) == juce::Colour{0xff2a2f36});
    // And its letters ink somewhere inside the box. The text is white at 0.85 alpha over the
    // chrome, so its solid pixels blend to roughly (223, 224, 225) — far above anything the box
    // or the empty lane can produce, but below the pure-white isWhiteInk floor.
    bool found_ink = false;
    for (int y = 2; y <= 12 && !found_ink; ++y)
    {
        for (int x = 2; x <= 60 && !found_ink; ++x)
        {
            const juce::Colour pixel = image.getPixelAt(x, y);
            found_ink = pixel.getRed() >= 200 && pixel.getGreen() >= 200 && pixel.getBlue() >= 200;
        }
    }
    CHECK(found_ink);

    // Without a capo the corner stays empty lane.
    state.capo = 0;
    const juce::Image bare{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics bare_graphics{bare};
    paintTabLane(bare_graphics, metrics, state, prefix_max);
    CHECK(bare.getPixelAt(4, 7).getAlpha() == 0);
}

// A tail looks the same however the repaint is clipped. Both wavy tail overlays generate only the
// stretch the clip can show, which is what keeps a note tail's cost proportional to the visible
// width rather than to its own length; that only holds up if the narrowed render lands what the
// whole-lane render would have. A repaint window is an arbitrary rectangle, so it cuts mid-tail
// all the time, and a seam, a dropped tooth, or a shifted phase at its edge would surface here as
// a column that disagrees.
TEST_CASE("Tab paint core paints a tail the same under any clip", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::TabViewState state;
    state.string_count = 6;
    // Two tails far longer than the window they are probed in: a tremolo band and a vibrato sine,
    // the two overlays whose geometry is generated per apex and per pixel.
    state.notes = {
        common::core::TabNoteView{
            .start_seconds = 1.0,
            .end_seconds = 18.0,
            .string = 2,
            .fret = 7,
            .tremolo = true,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 1.0,
            .end_seconds = 18.0,
            .string = 4,
            .fret = 9,
            .vibrato = true,
            .bend = {},
            .slides = {},
        },
    };

    const juce::Rectangle<int> bounds{0, 0, 400, 240};
    const common::core::TimeRange visible_timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    const TabLaneMetrics metrics = makeTabLaneMetrics(
        bounds,
        visible_timeline,
        common::core::displayedStringCount(state.string_count, 0),
        state.string_count);
    const std::vector<double> prefix_max_end = resolveHoldEnds(state);

    const juce::Image whole{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics whole_graphics{whole};
    paintTabLane(whole_graphics, metrics, state, prefix_max_end);

    // A window deep inside both tails, so its every column is tail interior — no onset, no head,
    // no tail end.
    const juce::Rectangle<int> window{160, 0, 40, 240};
    const juce::Image narrowed{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics narrowed_graphics{narrowed};
    narrowed_graphics.reduceClipRegion(window);
    paintTabLane(narrowed_graphics, metrics, state, prefix_max_end);

    int worst_delta = 0;
    int worst_x = 0;
    int worst_y = 0;
    double covered = 0.0;
    for (int x = window.getX(); x < window.getRight(); ++x)
    {
        for (int y = 0; y < bounds.getHeight(); ++y)
        {
            const juce::Colour from_whole = whole.getPixelAt(x, y);
            const juce::Colour from_narrowed = narrowed.getPixelAt(x, y);
            covered += static_cast<double>(from_whole.getAlpha()) / 255.0;
            const int delta = std::max(
                {std::abs(from_whole.getAlpha() - from_narrowed.getAlpha()),
                 std::abs(from_whole.getRed() - from_narrowed.getRed()),
                 std::abs(from_whole.getGreen() - from_narrowed.getGreen()),
                 std::abs(from_whole.getBlue() - from_narrowed.getBlue())});
            if (delta > worst_delta)
            {
                worst_delta = delta;
                worst_x = x;
                worst_y = y;
            }
        }
    }
    CAPTURE(worst_x, worst_y);
    // Both generated overlays land bit-identical; the residue is JUCE's own antialiasing of the
    // plain ribbon, a float-height fillRect whose two edge rows already came out a step lighter or
    // darker with the clip before any of this existed (it is still there with both overlays
    // switched off). Two steps of 255 on a soft edge is invisible, and it is four decimal orders
    // below what an actual seam scores: a dropped tooth swaps ribbon for empty lane at full alpha.
    CHECK(worst_delta <= 2);
    // And the window really did hold both tails, so a pass cannot come from comparing two empty
    // regions. Two ribbons ~10px tall across 40 columns clear this floor several times over.
    CHECK(covered > 200.0);
}

// A ghost note is quieted by COLOR, never by opacity, and that distinction is the whole design
// rather than a detail. This lane is opaque and composites by covering: a head that went
// translucent would show its own sustain ribbon through its right half, plus the lane line and
// anything else beneath it, and it would need a knockout to stay correct. Leaning every ink toward
// the lane's ground buys the identical weight with none of that, so the picture must come out the
// same shape at the same opacity and merely darker.
//
// Three claims, each killing a different way the axis can regress: drawing the ghost identically
// to a normal note (the state before this existed), quieting it by alpha (which the surface's own
// compositing model forbids), and quieting only the string-derived ink while the loudest mark on
// the note — the white fret digit — stays at full strength.
TEST_CASE("Tab paint core quiets a ghost note by color, not by opacity", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const TabLaneMetrics metrics = referenceMetrics(6);
    const auto painted = [&metrics](const common::core::NoteEmphasis emphasis) {
        common::core::TabViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::TabNoteView{
                .start_seconds = 5.0,
                .end_seconds = 9.0,
                .string = 3,
                .fret = 7,
                .emphasis = emphasis,
                .bend = {},
                .slides = {},
            },
        };
        const std::vector<double> prefix_max = resolveHoldEnds(state);
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(graphics, metrics, state, prefix_max);
        return image;
    };

    const juce::Image normal = painted(common::core::NoteEmphasis::Normal);
    const juce::Image ghost = painted(common::core::NoteEmphasis::Ghost);

    // It is drawn differently at all.
    CHECK(worstPixelDelta(normal, ghost) > 0);

    // ...but at the SAME opacity everywhere, which is the claim a transparency treatment could not
    // make. Every ink is opaque before and after leaning, so the two renders differ only in color:
    // identical silhouettes, identical antialiased edges, and a head that still covers what is
    // under it.
    //
    // Every channel also moves TOWARD the lane's ground and never past it. Toward, not merely
    // down: a dark string's ink can sit BELOW the ground in a channel it barely uses — the red
    // string's tail fill carries no green at all — and leaning lifts that channel the few counts
    // the ground holds. That is not a defect to clamp away, it is precisely what the same note
    // drawn translucent over this ground would show, which is what makes the quiet weight here
    // mean the same thing it means on the highway.
    constexpr int ground_channel = 0x10;
    // One count of slack, and only one: JUCE tweens in premultiplied 8-bit integers, so a channel
    // already sitting ON the ground can quantize a count to either side of it. That rounding is
    // not a direction, and nothing this test is defending against — alpha, a no-op, a brightening
    // — can hide inside a single count.
    constexpr int quantization_slack = 1;
    int worst_alpha_delta = 0;
    bool any_moved = false;
    bool any_overshot = false;
    juce::Point<int> overshot_at{-1, -1};
    juce::Colour overshot_was;
    juce::Colour overshot_now;
    const auto leans_toward_ground = [&](const int was, const int now) {
        any_moved = any_moved || now != was;
        return std::abs(now - ground_channel) <=
               std::abs(was - ground_channel) + quantization_slack;
    };
    for (int x = 0; x < normal.getWidth(); ++x)
    {
        for (int y = 0; y < normal.getHeight(); ++y)
        {
            const juce::Colour from_normal = normal.getPixelAt(x, y);
            const juce::Colour from_ghost = ghost.getPixelAt(x, y);
            worst_alpha_delta = std::max(
                worst_alpha_delta, std::abs(from_normal.getAlpha() - from_ghost.getAlpha()));
            const bool leans = leans_toward_ground(from_normal.getRed(), from_ghost.getRed()) &&
                               leans_toward_ground(from_normal.getGreen(), from_ghost.getGreen()) &&
                               leans_toward_ground(from_normal.getBlue(), from_ghost.getBlue());
            if (!leans && !any_overshot)
            {
                overshot_at = {x, y};
                overshot_was = from_normal;
                overshot_now = from_ghost;
            }
            any_overshot = any_overshot || !leans;
        }
    }
    CHECK(worst_alpha_delta == 0);
    CAPTURE(overshot_at.toString(), overshot_was.toString(), overshot_now.toString());
    CHECK_FALSE(any_overshot);
    CHECK(any_moved);

    // The fret digit quiets with the note. It is the loudest ink a note carries and the one that
    // does NOT come from the string palette, so a ghost whose digit is still pure white is a ghost
    // quieted through the string colors alone — which is exactly the shape this ink set exists to
    // make impossible.
    const int onset_x = juce::roundToInt(metrics.x(5.0));
    const int center_y = juce::roundToInt(metrics.laneY(3));
    CHECK(topDigitInkRow(normal, onset_x, center_y) > 0);
    CHECK(topDigitInkRow(ghost, onset_x, center_y) == 0);

    // And the whole note quiets at ONE weight — head and sustain alike. That is the claim the
    // single ghost constant makes, and it is worth pinning as an equality rather than as two
    // numbers: the earlier design leaned the head and the ribbon differently, and the way that
    // regresses is for one of them to drift while the other stays put. Measured as the share of
    // each sample's distance above the lane's ground that survives — the head's fill against the
    // tail's bright rail, well past the head.
    constexpr double ground = 16.0; // the lane's own near-black, which every ink leans toward
    const auto retained = [&](const int x, const int y) {
        const juce::Colour was = normal.getPixelAt(x, y);
        const juce::Colour now = ghost.getPixelAt(x, y);
        const double above = (was.getRed() + was.getGreen() + was.getBlue()) - (3.0 * ground);
        const double left = (now.getRed() + now.getGreen() + now.getBlue()) - (3.0 * ground);
        REQUIRE(above > 0.0);
        return left / above;
    };
    // Inside the head's fill, clear of the single digit's glyph; and on the tail's top rail, sixty
    // columns downstream where nothing but the ribbon is drawn.
    const double head_retained =
        retained(onset_x + juce::roundToInt(metrics.headSize() * 0.32f), center_y);
    const int rail_y = juce::roundToInt(
        static_cast<float>(center_y) - (metrics.tail_height / 3.0f) - 1.0f +
        (metrics.tail_edge_size / 2.0f));
    const double tail_retained = retained(onset_x + 60, rail_y);
    CAPTURE(head_retained, tail_retained);
    CHECK_THAT(head_retained, Catch::Matchers::WithinAbs(0.5, 0.02));
    CHECK_THAT(tail_retained, Catch::Matchers::WithinAbs(0.5, 0.02));
    CHECK_THAT(tail_retained, Catch::Matchers::WithinAbs(head_retained, 0.02));
}

// A tail that ends bare — no cap — is only correct if every mark riding it runs the whole ribbon.
// Both marks used to inset their final endpoint by one stroke to meet a cap that no longer exists,
// which showed as a stub of bare ribbon past the mark's tip. Pinned as "the mark's ink reaches the
// ribbon's last column", the user-visible claim, rather than as arithmetic on the inset — and
// pinned for the slide AND the bend, because the inset was two separate lines and dropping one
// would leave the other's stub in place.
TEST_CASE("Tab paint core runs a tail's marks to the end of its ribbon", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const TabLaneMetrics metrics = referenceMetrics(6);
    const auto painted = [&metrics](const common::core::TabNoteView& note) {
        common::core::TabViewState state;
        state.string_count = 6;
        state.notes = {note};
        const std::vector<double> prefix_max = resolveHoldEnds(state);
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(graphics, metrics, state, prefix_max);
        return image;
    };

    // The mark's bright ink anywhere in the tail's band at one column. Scanned across the band
    // rather than at a computed height because a climbing diagonal and a held bend run leave their
    // ink at different heights, and what is being pinned is that the ink is THERE.
    const int end_x = juce::roundToInt(metrics.x(9.0));
    const auto mark_reaches = [&](const juce::Image& image, const int column) {
        const int center_y = juce::roundToInt(metrics.laneY(3));
        for (int y = center_y - 12; y <= center_y + 12; ++y)
        {
            if (isWhiteInk(image.getPixelAt(column, y)))
            {
                return true;
            }
        }
        return false;
    };

    SECTION("a glide whose last waypoint lands on the sustain end")
    {
        CHECK(mark_reaches(
            painted(
                common::core::TabNoteView{
                    .start_seconds = 5.0,
                    .end_seconds = 9.0,
                    .string = 3,
                    .fret = 5,
                    .bend = {},
                    .slides =
                        {common::core::TabSlideView{.seconds = 9.0, .fret = 9, .linked = false}},
                }),
            end_x - 1));
    }

    SECTION("a bend's held run after its last bend point")
    {
        CHECK(mark_reaches(
            painted(
                common::core::TabNoteView{
                    .start_seconds = 5.0,
                    .end_seconds = 9.0,
                    .string = 3,
                    .fret = 5,
                    .bend = {common::core::TabBendPointView{.seconds = 7.0, .semitones = 2.0}},
                    .slides = {},
                }),
            end_x - 1));
    }
}

// The pending entry box: the editor's provisional-value chrome, exported from the core so the
// digit's plate and typography stay the committed head's. Pins the inks the primitive promises
// on BOTH plate polarities: the host's text color at the glyphs, the host's border color on the
// frame, and the known ground — near-black under a valid value, white under an invalid one
// (the plate flip is itself the glance signal).
TEST_CASE("Tab paint core draws the pending entry box in the host's inks", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const juce::Colour border{0xff87cefa};
    // Renders one box and probes exact text-ink and ground pixels plus the frame's presence.
    // The glyph cores and the fill's interior land at full strength, so those probe exactly;
    // the one-pixel frame sits on fractional edges and antialiases everywhere, so it probes by
    // hue instead — the accent is the only blue-dominant ink in either image.
    const auto probe = [&border](
                           const bool light_plate,
                           const juce::Colour ink,
                           const juce::Colour ground) {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabPendingEntryBox(
            graphics, referenceMetrics(6), nullptr, 200.0f, 120.0f, "17", light_plate, ink, border);
        int text_pixels = 0;
        int border_pixels = 0;
        int ground_pixels = 0;
        for (int y = 90; y <= 150; ++y)
        {
            for (int x = 160; x <= 240; ++x)
            {
                const juce::Colour pixel = image.getPixelAt(x, y);
                text_pixels += pixel == ink ? 1 : 0;
                border_pixels +=
                    pixel.getAlpha() > 0 && pixel.getBlue() > pixel.getRed() + 24 ? 1 : 0;
                ground_pixels += pixel == ground ? 1 : 0;
            }
        }
        CHECK(text_pixels > 4);
        CHECK(border_pixels > 8);
        CHECK(ground_pixels > 20);
    };

    probe(/*light_plate=*/false, juce::Colour{0xffffffff}, juce::Colour{0xff101010});
    probe(/*light_plate=*/true, juce::Colour{0xffff0000}, juce::Colour{0xffffffff});

    // The placement rule rides along: over a plectrum head the whole box lifts by the head's
    // own digit raise, so the provisional number sits exactly where the committed one lands.
    const auto top_ink_row = [](const juce::Image& image, const juce::Colour ink) {
        for (int y = 60; y <= 180; ++y)
        {
            for (int x = 160; x <= 240; ++x)
            {
                if (image.getPixelAt(x, y) == ink)
                {
                    return y;
                }
            }
        }
        return -1;
    };
    const auto painted_box = [&border](const common::core::TabNoteView* note) {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabPendingEntryBox(
            graphics,
            referenceMetrics(6),
            note,
            200.0f,
            120.0f,
            "17",
            /*light_plate=*/false,
            juce::Colour{0xffffffff},
            border);
        return image;
    };
    const common::core::TabNoteView scrape{
        .start_seconds = 5.0,
        .end_seconds = 6.0,
        .string = 3,
        .fret = 9,
        .attack = common::core::NoteAttack::PickSlide,
        .legato = common::core::LegatoMotion::Unjustified,
        .bend = {},
        .slides = {},
    };
    const int plain_top = top_ink_row(painted_box(nullptr), juce::Colour{0xffffffff});
    const int scrape_top = top_ink_row(painted_box(&scrape), juce::Colour{0xffffffff});
    REQUIRE(plain_top > 0);
    REQUIRE(scrape_top > 0);
    CHECK(scrape_top < plain_top);
}

} // namespace rock_hero::common::ui
