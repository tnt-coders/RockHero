#include "highway/highway_atlas.h"
#include "highway/structural_art.h"
#include "tab/plectrum_outline.h"

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// The vibrato regions a note shaking END TO END carries: exactly what the projection derives from
// a chart that states vibrato at the onset and never restates it, which is every chart written
// before the channel could say anything else.
[[nodiscard]] std::vector<common::core::VibratoSpanViewState> wholeTailShake(
    const double start_seconds, const double end_seconds)
{
    return {common::core::VibratoSpanViewState{
        .start_seconds = start_seconds, .end_seconds = end_seconds
    }};
}

// The height in ROWS of everything one render inks that another does not, across a window of
// columns. Where a mark's SWING is the question, the band it occupies is the answer, and asking it
// as a difference against the same picture without the mark needs no knowledge of the mark's own
// geometry — exactly as worstPixelDeltaInColumns asks where a mark begins.
[[nodiscard]] int inkBandRows(
    const juce::Image& painted, const juce::Image& without, const int x_from, const int x_to)
{
    int top = painted.getHeight();
    int bottom = -1;
    for (int y = 0; y < painted.getHeight(); ++y)
    {
        for (int x = x_from; x <= x_to; ++x)
        {
            if (painted.getPixelAt(x, y) != without.getPixelAt(x, y))
            {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                break;
            }
        }
    }
    return bottom < top ? 0 : (bottom - top) + 1;
}

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

// How white one pixel is, on a single scale: the weakest of its four channels, so a pixel scores
// high only when it is opaque AND unblended toward any hue. What matters is not the number but
// that it rises monotonically with how much white ink covers the pixel, over ANY backdrop.
[[nodiscard]] int whiteness(juce::Colour color)
{
    return std::min(
        {static_cast<int>(color.getAlpha()),
         static_cast<int>(color.getRed()),
         static_cast<int>(color.getGreen()),
         static_cast<int>(color.getBlue())});
}

// True when `color` lies nearer `first` than `second` in straight ARGB distance. Used where a
// probe knows both colors an area can hold and needs to say which one a partially covered pixel
// belongs to, without inventing a tolerance to compare against.
[[nodiscard]] bool nearerTo(juce::Colour color, juce::Colour first, juce::Colour second)
{
    const auto distance = [](const juce::Colour lhs, const juce::Colour rhs) {
        const int alpha = lhs.getAlpha() - rhs.getAlpha();
        const int red = lhs.getRed() - rhs.getRed();
        const int green = lhs.getGreen() - rhs.getGreen();
        const int blue = lhs.getBlue() - rhs.getBlue();
        return (alpha * alpha) + (red * red) + (green * green) + (blue * blue);
    };
    return distance(color, first) < distance(color, second);
}

// Half-width of the head's digit window. Narrower than the beside-head chip's own clearance from
// the axis, so within it only the fret number can be white.
constexpr int g_digit_window = 4;

// Topmost row carrying the digit's densest ink inside that window; 0 when nothing is painted
// there at all.
//
// The row is located by each image's OWN peak whiteness rather than by any fixed ink test, and
// that is what makes it comparable ACROSS images. How white a partially covered pixel lands
// depends on what lies beneath it: over a ghost's faded note group the same coverage reads much
// closer to white than over an opaque one (measured 2026-08-25 at the fret digit -- 170/199/208
// against 131/173/186 on the same row). Any absolute test therefore crosses on a DIFFERENT row in
// two images of the SAME glyph, by however much the platform's antialiasing ramp says, which is
// how a 250-per-channel threshold agreed on Windows and disagreed by five rows under FreeType.
//
// Whiteness rises with coverage in both composites, so the row where it peaks is the row the
// glyph covers most -- a property of the glyph, not of the backdrop or the rasterizer. And a
// maximum always exists, so this cannot come up empty on a rasterizer that never fully covers a
// pixel, which is where demanding a solid pixel failed on CoreText.
[[nodiscard]] int topDigitInkRow(const juce::Image& image, int center_x, int center_y)
{
    int peak = 0;
    bool painted = false;
    for (int y = center_y - 20; y <= center_y + 20; ++y)
    {
        for (int x = center_x - g_digit_window; x <= center_x + g_digit_window; ++x)
        {
            const juce::Colour pixel = image.getPixelAt(x, y);
            if (pixel.getAlpha() == 0)
            {
                continue;
            }
            painted = true;
            peak = std::max(peak, whiteness(pixel));
        }
    }
    if (!painted)
    {
        return 0;
    }
    for (int y = center_y - 20; y <= center_y + 20; ++y)
    {
        for (int x = center_x - g_digit_window; x <= center_x + g_digit_window; ++x)
        {
            const juce::Colour pixel = image.getPixelAt(x, y);
            if (pixel.getAlpha() > 0 && whiteness(pixel) == peak)
            {
                return y;
            }
        }
    }
    return 0;
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

// The largest per-channel difference between two renders across a range of COLUMNS, inclusive.
// Where a mark begins is a question about columns, so asking it as "do these two renders agree
// left of here, and disagree right of it" needs no knowledge of the mark's own geometry.
[[nodiscard]] int worstPixelDeltaInColumns(
    const juce::Image& lhs, const juce::Image& rhs, const int x_from, const int x_to)
{
    int worst = 0;
    for (int x = x_from; x <= x_to; ++x)
    {
        for (int y = 0; y < lhs.getHeight(); ++y)
        {
            const juce::Colour from_lhs = lhs.getPixelAt(x, y);
            const juce::Colour from_rhs = rhs.getPixelAt(x, y);
            worst = std::max(
                {worst,
                 std::abs(from_lhs.getAlpha() - from_rhs.getAlpha()),
                 std::abs(from_lhs.getRed() - from_rhs.getRed()),
                 std::abs(from_lhs.getGreen() - from_rhs.getGreen()),
                 std::abs(from_lhs.getBlue() - from_rhs.getBlue())});
        }
    }
    return worst;
}

// The largest per-channel difference anywhere between two renders: zero means the two are the same
// picture, which is the only way to state "draws exactly what the plain pick draws" without
// depending on where a mark's geometry happens to land.
[[nodiscard]] int worstPixelDelta(const juce::Image& lhs, const juce::Image& rhs)
{
    return worstPixelDeltaInColumns(lhs, rhs, 0, lhs.getWidth() - 1);
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
        common::core::ChartViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::NoteViewState{
                .start_seconds = 5.0,
                .end_seconds = 9.0,
                .string = 3,
                .fret = 7,
                .attack = attack,
                .legato = motion,
                .bend = {},
                .slides = {},
                .vibrato = {},
            },
        };
        const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
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
// quiet end of this axis already said so here by fading the entire ink set, ribbon included, so a
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
        common::core::ChartViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::NoteViewState{
                .start_seconds = 5.0,
                .end_seconds = 9.0,
                .string = 3,
                .fret = 7,
                .emphasis = emphasis,
                .bend = {},
                .slides = {},
                .vibrato = {},
            },
        };
        const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(graphics, referenceMetrics(state.string_count), state, prefix_max);
        return image;
    };

    const juce::Image plain = painted(common::core::NoteEmphasis::Normal);
    const juce::Image accented = painted(common::core::NoteEmphasis::Accent);

    const common::core::NoteViewState probe{
        .start_seconds = 5.0,
        .end_seconds = 9.0,
        .string = 3,
        .fret = 7,
        .bend = {},
        .slides = {},
        .vibrato = {},
    };
    const TabNoteLayout layout = tabNoteLayout(referenceMetrics(6), probe);

    const auto worst_in_band =
        [&plain, &accented](const int x_from, const int x_to, const int y_from, const int y_to) {
            int worst = 0;
            for (int x = x_from; x <= x_to; ++x)
            {
                for (int y = y_from; y <= y_to; ++y)
                {
                    const juce::Colour from_plain = plain.getPixelAt(x, y);
                    const juce::Colour from_accented = accented.getPixelAt(x, y);
                    worst = std::max(
                        {worst,
                         std::abs(from_plain.getAlpha() - from_accented.getAlpha()),
                         std::abs(from_plain.getRed() - from_accented.getRed()),
                         std::abs(from_plain.getGreen() - from_accented.getGreen()),
                         std::abs(from_plain.getBlue() - from_accented.getBlue())});
                }
            }
            return worst;
        };

    // The ribbon's own band, computed the way the painter computes it: from the note's onset — the
    // one column a drawn tail can start at — to its presented end, across Charter's tail rails. The
    // layout manifest no longer publishes a tail rectangle at all: heads are targets, tails are
    // testimony (user ruling 2026-08-30).
    const TabLaneMetrics probe_metrics = referenceMetrics(6);
    const TailSpan band = tailSpan(probe_metrics, layout.center_y);
    const float tail_x = probe_metrics.x(probe.start_seconds);
    const float tail_width = probe_metrics.x(probe.end_seconds) - tail_x;
    const int tail_end = juce::roundToInt(tail_x + tail_width);
    const int tail_top = juce::roundToInt(band.top);
    const int tail_bottom = juce::roundToInt(band.bottom);
    // Sampled well along the tail, clear of the head's own halo, so this cannot pass on the head
    // glow that was already there.
    const int mid_from = juce::roundToInt(tail_x + (tail_width * 0.6f));

    // The halo is present on BOTH rails.
    CHECK(worst_in_band(mid_from, mid_from + 4, tail_top - 4, tail_top - 1) > 0);
    CHECK(worst_in_band(mid_from, mid_from + 4, tail_bottom + 1, tail_bottom + 4) > 0);
    // And it stops with them: nothing past the tail's end, on any row the halo occupies.
    CHECK(worst_in_band(tail_end + 2, tail_end + 12, tail_top - 6, tail_bottom + 6) == 0);
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
        common::core::ChartViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::NoteViewState{
                .start_seconds = 5.0,
                .end_seconds = 9.0,
                .string = 3,
                .fret = 7,
                .attack = attack,
                .legato = motion,
                .bend = {},
                .slides = {},
                .vibrato = {},
            },
        };
        const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
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

// Every tail the lane draws is drawn to the note's own PRESENTED end, teeth and sine included, and
// the hit-test rectangle stops exactly where that ink does. The span-implied hold running past it
// is the 3D board's — it pins a chugged strum's heads there — and this surface must not spend
// it: the chug below presents no tail, so it draws none and lays out with none, while its hold
// says 12.0s. The hold reaches the paint core only as `display_hold_ends`, so no shape span is
// needed to state the case (a covering span is what the projection derives that field from).
TEST_CASE("Tab paint core draws tails to the presented end", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::ChartViewState state;
    state.string_count = 6;
    // Three notes at 2.0s presenting a tail to 8.0s (x = 160) — plain, tremolo, vibrato.
    const auto sustained = [](int string, bool tremolo, bool vibrato) {
        return common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 8.0,
            .string = string,
            .fret = 7,
            .tremolo = tremolo,
            .bend = {},
            .slides = {},
            .vibrato = vibrato ? wholeTailShake(2.0, 8.0)
                               : std::vector<common::core::VibratoSpanViewState>{},
        };
    };
    // And a chugged member of a strum a hand-shape span holds: no presented tail at all.
    // On string 6 with string 5 left empty between it and the sustained notes, so the per-string
    // probe below cannot read a neighbour's ribbon as this one's.
    const common::core::NoteViewState chug{
        .start_seconds = 2.0,
        .end_seconds = 2.0,
        .string = 6,
        .fret = 7,
        .bend = {},
        .slides = {},
        .vibrato = {},
    };
    state.notes = {
        sustained(2, false, false), sustained(3, true, false), sustained(4, false, true), chug
    };
    // Every hold runs to 12.0s (x = 240), well past all four: the board holds the three tails
    // longer than they present and pins the chug's head for the whole span.
    state.display_hold_ends = {12.0, 12.0, 12.0, 12.0};

    const TabLaneMetrics metrics = referenceMetrics(state.string_count);
    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    paintTabLane(graphics, metrics, state, common::core::makeSustainPrefixMax(state.notes));

    // The empty lane for reference: string lines run the full width, so "where the tail ends" must
    // be measured as a difference from the furniture rather than as raw coverage.
    const juce::Image lanes_only{juce::SoftwareImageType{}.create(
        juce::Image::ARGB, 400, 240, true)};
    {
        common::core::ChartViewState bare;
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

    for (const common::core::NoteViewState& note : state.notes)
    {
        CAPTURE(note.string);
        const TabNoteLayout layout = tabNoteLayout(metrics, note);
        // The head is drawn for all four, so the chug's missing ribbon below is a missing RIBBON
        // rather than a missing note.
        CHECK(differs(note.string, 40));

        const int last_column = last_inked_column(note.string);
        if (note.end_seconds > note.start_seconds)
        {
            // Ink well past the onset (x = 40) proves the ribbon was drawn, and the tremolo teeth
            // and the vibrato sine ride the presented length rather than their own.
            CHECK(differs(note.string, 120));
            // And it stops at the presented end (x = 160), not at the hold (x = 240) — the ink
            // agrees with the note's own stated end to the pixel, which is what the paint pass
            // reading that end off the note itself buys.
            CHECK(last_column <= 161);
            CHECK(std::abs(last_column - juce::roundToInt(metrics.x(note.end_seconds))) <= 1);
        }
        else
        {
            // The chug: no ribbon anywhere past its head. The old hold ribbon reached x = 240 here.
            CHECK_FALSE(differs(note.string, 120));
            CHECK(last_column <= juce::roundToInt(layout.head.x + layout.head.width));
            // Its HEAD is still the whole of what addresses it, ring or no ring.
            CHECK(layout.head.contains(metrics.x(note.start_seconds), metrics.laneY(note.string)));
        }
    }
}

// The sine covers exactly the stretch its region claims and no more — BOTH of its ends, because
// the channel states a shake's stop as readily as its start. This is the figure the keyframe
// model's vibrato channel exists for — a shake that starts where a glide arrives, which the
// whole-note flag could only draw from the onset, across the travel it never touched.
TEST_CASE("Tab paint core draws a vibrato sine only over its stated region", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    // One note from 2.0s to 8.0s. The reference metrics run 20 seconds across 400 pixels, so a
    // second is 20 pixels: the onset lands at x = 40, the tail ends at x = 160, and a region
    // boundary stated at 5.0s falls at x = 100.
    const auto painted = [](std::vector<common::core::VibratoSpanViewState> vibrato) {
        common::core::ChartViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::NoteViewState{
                .start_seconds = 2.0,
                .end_seconds = 8.0,
                .string = 3,
                .fret = 7,
                .bend = {},
                .slides = {},
                .vibrato = std::move(vibrato),
            },
        };
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(
            graphics,
            referenceMetrics(state.string_count),
            state,
            common::core::makeSustainPrefixMax(state.notes));
        return image;
    };

    const juce::Image steady = painted({});
    const juce::Image late =
        painted({common::core::VibratoSpanViewState{.start_seconds = 5.0, .end_seconds = 8.0}});
    const juce::Image early =
        painted({common::core::VibratoSpanViewState{.start_seconds = 2.0, .end_seconds = 5.0}});
    const juce::Image throughout = painted(wholeTailShake(2.0, 8.0));

    // Left of the statement the shaking note and the steady one are the SAME picture: the sine
    // starts where the chart says it starts, not where the note does.
    CHECK(worstPixelDeltaInColumns(late, steady, 0, 92) == 0);
    // ...and right of it they differ, so the identity above is not an empty render.
    CHECK(worstPixelDeltaInColumns(late, steady, 108, 158) > 0);
    // The discrimination the first check needs: a region covering the whole tail DOES ink those
    // same early columns, so the probe can see a sine there when one is drawn — and the old
    // whole-note flag drew exactly this picture for the late shake too.
    CHECK(worstPixelDeltaInColumns(throughout, steady, 0, 92) > 0);
    CHECK(worstPixelDeltaInColumns(throughout, late, 0, 92) > 0);

    // The region's other end, which is the same rule read backwards: a shake the channel STOPS
    // mid-tail inks nothing past the stop, while the tail itself runs on to 8.0s underneath. A
    // sine drawn to the note's end instead of the region's would pass every check above.
    CHECK(worstPixelDeltaInColumns(early, steady, 108, 158) == 0);
    // ...and it is the same wave as the whole-tail one up to that stop, so the identity is a
    // region that ENDED rather than one that was never drawn.
    CHECK(worstPixelDeltaInColumns(early, throughout, 0, 92) == 0);
    CHECK(worstPixelDeltaInColumns(early, steady, 0, 92) > 0);
    // The discrimination the stop needs: those late columns are exactly where the whole-tail
    // region does ink, so the equality above is a sine that stopped, not a blind window.
    CHECK(worstPixelDeltaInColumns(throughout, steady, 108, 158) > 0);

    // The WIDTH each region carries, which is the only thing separating these two renders: same
    // note, same stretch, same phase. A stroked sine occupies (2 * swing + stroke) rows, so the
    // wide band is the multiplier applied to the ordinary swing with the stroke added back — read
    // off the constant, because a literal here would pass while the two tiers had silently come
    // apart.
    std::vector<common::core::VibratoSpanViewState> wide_regions = wholeTailShake(2.0, 8.0);
    wide_regions.front().state = common::core::VibratoState::Wide;
    const juce::Image wide = painted(std::move(wide_regions));
    const double stroke =
        static_cast<double>(std::max(1.0f, referenceMetrics(6).tail_height / 8.0f));
    const double narrow_band = inkBandRows(throughout, steady, 42, 158);
    const double wide_band = inkBandRows(wide, steady, 42, 158);
    CHECK(
        wide_band ==
        Catch::Approx(
            (static_cast<double>(g_wide_vibrato_swing_multiplier) * (narrow_band - stroke)) +
            stroke)
            .margin(2.0));
    // ...and the pair really is a pair: the ordinary tier leaves room above it rather than
    // already filling the band, which is what makes the wide one visible at all.
    CHECK(wide_band > narrow_band);
}

// Techniques, shape spans, and fret-hand positions all draw without touching empty lanes.
// Moved from the editor's TabView suite when the paint core was extracted (plan 30 Phase 2);
// every probe color is unchanged, so the core's output is pinned to the editor lane's shipped
// pixels.
TEST_CASE("Tab paint core draws techniques, shapes, and fret-hand positions", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::ChartViewState state;
    state.string_count = 6;
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 8.0,
            .string = 1,
            .fret = 5,
            // A stored claim plus the motion it resolved to: the mark comes from the resolution, so
            // the projection's answer is what the paint core has to be handed.
            .attack = common::core::NoteAttack::Legato,
            .legato = common::core::LegatoMotion::Hammer,
            .palm_mute = true,
            .emphasis = common::core::NoteEmphasis::Accent,
            .bend = {common::core::BendPointViewState{.seconds = 4.0, .semitones = 2.0}},
            .slides = {common::core::KeyframeViewState{
                .seconds = 7.0, .fret = 9, .offset = common::core::Fraction{}
            }},
            .vibrato = wholeTailShake(2.0, 8.0),
        },
        common::core::NoteViewState{
            .start_seconds = 3.0,
            .end_seconds = 6.0,
            .string = 2,
            .fret = 12,
            .harmonic_node = 12.0,
            .tremolo = true,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
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
            .vibrato = {},
        },
    };
    state.shapes = {
        common::core::ShapeViewState{
            .start_seconds = 2.0,
            .end_seconds = 6.0,
            .arpeggio = false,
            .strings = {},
        },
        common::core::ShapeViewState{
            .start_seconds = 10.0,
            .end_seconds = 12.0,
            .arpeggio = true,
            .strings =
                {
                    common::core::ShapeStringViewState{.string = 3, .fret = 7},
                    common::core::ShapeStringViewState{.string = 5, .fret = 8},
                },
            // WHERE the bracket draws is the projection's answer too, and an ordinary span's is
            // its own start. Absent would mean a span drawing no bracket at all, which only a
            // never-sounding landing successor ever is.
            .bracket_seconds = 10.0,
        },
    };
    state.fret_hand_positions = {
        common::core::FhpViewState{.seconds = 2.0, .fret = 5, .width = 4},
        // Wider than the standard four-fret hand: the marker spells out the inclusive range.
        common::core::FhpViewState{.seconds = 14.0, .fret = 3, .width = 5},
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
    const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
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
    common::core::ChartViewState state;
    state.string_count = 6;
    state.notes = {
        // Rings across the span start on string 3 with vibrato, so its sine crosses the side
        // chip's column — the ink the chip's ground exists to mask.
        common::core::NoteViewState{
            .start_seconds = 7.0,
            .end_seconds = 13.0,
            .string = 3,
            .fret = 7,
            .bend = {},
            .slides = {},
            .vibrato = wholeTailShake(7.0, 13.0),
        },
        // The tap: span-start onset on the same string at a fret the posture does not hold.
        common::core::NoteViewState{
            .start_seconds = 10.0,
            .end_seconds = 10.0,
            .string = 3,
            .fret = 12,
            .attack = common::core::NoteAttack::Tap,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
    };
    state.shapes = {
        common::core::ShapeViewState{
            .start_seconds = 10.0,
            .end_seconds = 14.0,
            .arpeggio = true,
            // WHICH column each digit takes is the projection's answer, published on the entry
            // (user ruling 2026-08-27) — this state states it directly, which is what makes the
            // painter's job drawing rather than deriving. String 3 is the displaced case (the tap
            // above sounds a different fret there) and string 5 the centred one.
            .strings =
                {
                    common::core::ShapeStringViewState{
                        .string = 3, .fret = 7, .digit = common::core::StopMarkSlot::Satellite
                    },
                    common::core::ShapeStringViewState{
                        .string = 5, .fret = 8, .digit = common::core::StopMarkSlot::Bracket
                    },
                },
            .bracket_seconds = 10.0,
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
    const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
    paintTabLane(graphics, metrics, state, prefix_max);

    // Six lanes in 240px: string 3 renders at lane center y = 140, string 5 at y = 60. The span
    // start (10.0s) lands at x = 200; the bracket's closing bar ends at x = 216, so the side
    // chip's ground starts on column 216. Its WIDTH is the lane's satellite slot rather than the
    // digit's own ink — one column for every satellite, and the same rectangle the hit test
    // bounds — so the columns this reads are derived from that slot instead of restated here,
    // which is what keeps the probes pointing at the chip if the slot is ever resized.
    const common::ui::TabSatelliteSlot slot = metrics.satelliteSlot();
    const int chip_left = 216;
    const int chip_right = chip_left + slot.extent() - 1;
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
    CHECK(white_in(chip_left + slot.gap, chip_right - slot.gap, 135, 144));

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
    CHECK(sine_grey_in(chip_right + 2, chip_right + 8, 140, 148));

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
        common::core::NoteViewState note;
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
    common::core::ChartViewState state;
    state.string_count = 6;
    // Four zero-length notes on one lane, differing only in what should change the head. Zero
    // length keeps every head clean: drawNoteTail returns early, so no sustain ribbon reaches
    // the probes.
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 4.0,
            .end_seconds = 4.0,
            .string = 3,
            .fret = 5,
            .attack = common::core::NoteAttack::PickSlide,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 8.0,
            .end_seconds = 8.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .dead = true,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 16.0,
            .end_seconds = 16.0,
            .string = 3,
            .fret = 5,
            .harmonic_node = 5.0,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        // The widest number the raise has to hold, on its own lane: two digits reach far enough
        // left to meet the chip that caps the raise, which one digit never does.
        common::core::NoteViewState{
            .start_seconds = 4.0,
            .end_seconds = 4.0,
            .string = 5,
            .fret = 12,
            .attack = common::core::NoteAttack::PickSlide,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 8.0,
            .end_seconds = 8.0,
            .string = 5,
            .fret = 12,
            .bend = {},
            .slides = {},
            .vibrato = {},
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
    const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
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

// A pinch harmonic's head is its FRETTED stop's head, exactly as the highway draws it: both
// surfaces today show only the pinch's left-hand half, and its node lies off the neck where the
// thumb grazes, so the diamond — which names a node the fretting hand stands on — would claim a
// node the hand is nowhere near. How the right-hand node will be shown is an open question; until
// it is ruled, the head shape and the head text read the same sounding rule and cannot disagree.
// The lane used to diamond ANY note with a node, which put a pinch in a shape the board never gave
// it.
TEST_CASE("Tab paint core heads a pinch at its fretted stop", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::core::ChartViewState state;
    state.string_count = 6;
    // Three zero-length notes on one lane: a plain pick, a pinch on the same fret, and a natural
    // harmonic whose node IS on the neck. Zero length keeps every head clean of tails.
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 4.0,
            .end_seconds = 4.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 8.0,
            .end_seconds = 8.0,
            .string = 3,
            .fret = 5,
            .attack = common::core::NoteAttack::Pinch,
            .harmonic_node = 24.0,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .harmonic_node = 5.0,
            .bend = {},
            .slides = {},
            .vibrato = {},
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
    REQUIRE_THAT(metrics.note_height, Catch::Matchers::WithinULP(25.0f, 0));

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
    paintTabLane(graphics, metrics, state, prefix_max);

    constexpr int plain_x = 80;
    constexpr int pinch_x = 160;
    constexpr int harmonic_x = 240;
    constexpr int lane_y = 140; // string 3 of six in 240 px

    // Eight rows above the string line a disc still spans most of its radius while a diamond has
    // tapered to a sliver, so one row's coverage tells the two silhouettes apart. The pinch must
    // match the disc to the pixel and the natural must not — the second check is what proves the
    // probe can see the difference at all.
    const double plain_row = rowCoverage(image, lane_y - 8, plain_x, plain_x + 15);
    const double pinch_row = rowCoverage(image, lane_y - 8, pinch_x, pinch_x + 15);
    const double harmonic_row = rowCoverage(image, lane_y - 8, harmonic_x, harmonic_x + 15);
    CHECK(std::abs(pinch_row - plain_row) < 0.5);
    CHECK(plain_row - harmonic_row > 3.0);
}

// The plectrum table is a hand-kept copy of a measurement of the shipped note atlas, which is the
// one shape of constant that drifts silently when art is rebaked. This re-measures the pick-slide
// cell's 0.5-coverage contour from the committed bytes and holds every interior row of the table
// to it, so a rebake that moves the silhouette fails here instead of leaving the lane's head
// disagreeing with the board's.
TEST_CASE("The plectrum table matches the shipped pick-slide art", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const juce::File art = juce::File{ROCK_HERO_TEXTURES_DIR}.getChildFile(
        std::string{common::core::highwayTextureFileName(common::core::HighwayTexture::Notes)});
    REQUIRE(art.existsAsFile());
    juce::MemoryBlock bytes;
    REQUIRE(art.loadFileAsData(bytes));
    const std::expected<juce::Image, StructuralArtError> decoded = decodeStructuralArtPng(
        std::span{static_cast<const std::byte*>(bytes.getData()), bytes.getSize()});
    REQUIRE(decoded.has_value());
    if (!decoded.has_value())
    {
        return;
    }

    // The pick-slide cell, addressed exactly as the atlas layout addresses it.
    const int cell_size = decoded->getWidth() / g_head_atlas_columns;
    REQUIRE(cell_size >= 16);
    const int x0 = (g_head_cell_pick_slide % g_head_atlas_columns) * cell_size;
    const int y0 = (g_head_cell_pick_slide / g_head_atlas_columns) * cell_size;
    REQUIRE(y0 + cell_size <= decoded->getHeight());
    const juce::Image::BitmapData bitmap{*decoded, juce::Image::BitmapData::readOnly};
    // Coverage is the B channel of the structural scheme; sample i owns [i - 0.5, i + 0.5].
    const auto coverage = [&](const int x, const int y) {
        return static_cast<double>(bitmap.getPixelColour(x0 + x, y0 + y).getFloatBlue());
    };
    // The interpolated position along one row where coverage falls back below 0.5, scanned from
    // the right — the contour's right edge on that row. Empty when the row carries no art.
    const auto right_crossing = [&](const int y) -> std::optional<double> {
        for (int x = cell_size - 1; x > 0; --x)
        {
            const double here = coverage(x, y);
            const double left = coverage(x - 1, y);
            if (left >= 0.5 && here < 0.5)
            {
                return (x - 1) + (left - 0.5) / (left - here);
            }
        }
        return std::nullopt;
    };
    const auto left_crossing = [&](const int y) -> std::optional<double> {
        for (int x = 0; x + 1 < cell_size; ++x)
        {
            const double here = coverage(x, y);
            const double right = coverage(x + 1, y);
            if (here < 0.5 && right >= 0.5)
            {
                return x + (0.5 - here) / (right - here);
            }
        }
        return std::nullopt;
    };
    // The silhouette's box at the 0.5 line: its top and bottom from the centre column's crossings,
    // its width from the widest row. The table's extent unit is the box HEIGHT, its origin the
    // box centre — the same construction the table documents.
    const int center_column = cell_size / 2;
    std::optional<double> top;
    std::optional<double> bottom;
    for (int y = 1; y < cell_size; ++y)
    {
        const double above = coverage(center_column, y - 1);
        const double here = coverage(center_column, y);
        if (!top.has_value() && above < 0.5 && here >= 0.5)
        {
            top = (y - 1) + (0.5 - above) / (here - above);
        }
        if (top.has_value() && above >= 0.5 && here < 0.5)
        {
            bottom = (y - 1) + (above - 0.5) / (above - here);
        }
    }
    REQUIRE(top.has_value());
    REQUIRE(bottom.has_value());
    if (!top.has_value() || !bottom.has_value())
    {
        return;
    }
    const double height = *bottom - *top;
    const double center_y = (*top + *bottom) / 2.0;
    auto widest_left = static_cast<double>(cell_size);
    double widest_right = 0.0;
    for (int y = 0; y < cell_size; ++y)
    {
        if (const auto left = left_crossing(y); left.has_value())
        {
            widest_left = std::min(widest_left, *left);
        }
        if (const auto right = right_crossing(y); right.has_value())
        {
            widest_right = std::max(widest_right, *right);
        }
    }
    const double center_x = (widest_left + widest_right) / 2.0;
    // The aspect the table documents: the art is 0.9395 as wide as it is tall.
    CHECK((widest_right - widest_left) / height == Catch::Approx(0.9395).margin(0.01));

    // Every interior row of the table against the contour, interpolated between the two texel
    // rows bracketing the table point's height. The two end points sit on the box's top and
    // bottom edges, where the contour has no row to read, and are pinned by the box itself.
    for (const juce::Point<float>& point : g_plectrum_half_outline)
    {
        if (std::abs(point.y) > 0.49f)
        {
            continue;
        }
        CAPTURE(point.x, point.y);
        const double row = center_y + (static_cast<double>(point.y) * height);
        const int row_below = static_cast<int>(std::floor(row));
        const std::optional<double> lower = right_crossing(row_below);
        const std::optional<double> upper = right_crossing(row_below + 1);
        REQUIRE(lower.has_value());
        REQUIRE(upper.has_value());
        if (!lower.has_value() || !upper.has_value())
        {
            return;
        }
        const double weight = row - row_below;
        const double crossing = (*lower * (1.0 - weight)) + (*upper * weight);
        const double measured_x = (crossing - center_x) / height;
        // Two thirds of a texel at the atlas's cell size. This row-interpolating tracer sits
        // within 0.011 of the table's own tracer on the committed art (measured 2026-08-21), and
        // a rebake that reshapes the silhouette moves its edges by whole texels (0.03 and up).
        CHECK(measured_x == Catch::Approx(static_cast<double>(point.x)).margin(0.02));
    }
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
        common::core::ChartViewState state;
        state.string_count = 6;
        common::core::NoteViewState note{
            .start_seconds = 4.0,
            .end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
            .vibrato = {},
        };
        if (scrape)
        {
            note.attack = common::core::NoteAttack::PickSlide;
            // The attack makes every stop unpitched pick travel; the last one is the required
            // terminal, which lands at the ring's end and so carries no time of its own.
            note.slides = {
                common::core::KeyframeViewState{
                    .seconds = 6.0, .fret = 9, .offset = common::core::Fraction{}
                },
                common::core::KeyframeViewState{
                    .seconds = 10.0, .fret = 3, .offset = common::core::Fraction{}
                },
            };
            note.slide_out = 12;
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
        const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
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
    // lane_y is constexpr, so it needs no capture.
    const auto top_row = [](const juce::Image& image, const int x) {
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
    common::core::ChartViewState state;
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
    const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
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
    common::core::ChartViewState state;
    state.string_count = 6;
    // Two tails far longer than the window they are probed in: a tremolo band and a vibrato sine,
    // the two overlays whose geometry is generated per apex and per pixel.
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 1.0,
            .end_seconds = 18.0,
            .string = 2,
            .fret = 7,
            .tremolo = true,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 1.0,
            .end_seconds = 18.0,
            .string = 4,
            .fret = 9,
            .bend = {},
            .slides = {},
            .vibrato = wholeTailShake(1.0, 18.0),
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
    const std::vector<double> prefix_max_end = common::core::makeSustainPrefixMax(state.notes);

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

// A ghost note keeps the normal note's RGB colors. Its tail, head art and technique marks flatten
// into one translucent group. Fret plates use a middle opacity while their numbers remain opaque.
//
// Isolated fully covered pixels expose the palette color directly, so they pin equal RGB alongside
// the chosen alpha without lane compositing obscuring either claim.
TEST_CASE("Tab paint core preserves color and fades a ghost note", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const TabLaneMetrics metrics = referenceMetrics(6);
    const auto painted =
        [&metrics](const common::core::NoteEmphasis emphasis, const bool dead = false) {
            common::core::ChartViewState state;
            state.string_count = 6;
            state.notes = {
                common::core::NoteViewState{
                    .start_seconds = 5.0,
                    .end_seconds = 9.0,
                    .string = 3,
                    .fret = 7,
                    .dead = dead,
                    .emphasis = emphasis,
                    .bend = {},
                    .slides = {},
                    .vibrato = {},
                },
            };
            const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
            const juce::Image image{juce::SoftwareImageType{}.create(
                juce::Image::ARGB, 400, 240, true)};
            juce::Graphics graphics{image};
            paintTabLane(graphics, metrics, state, prefix_max);
            return image;
        };

    const juce::Image normal = painted(common::core::NoteEmphasis::Normal);
    const juce::Image ghost = painted(common::core::NoteEmphasis::Ghost);
    const juce::Image muted_normal = painted(common::core::NoteEmphasis::Normal, true);
    const juce::Image muted_ghost = painted(common::core::NoteEmphasis::Ghost, true);

    // It is drawn differently at all.
    CHECK(worstPixelDelta(normal, ghost) > 0);

    int largest_alpha_drop = 0;
    int isolated_ink_pixels = 0;
    int worst_isolated_rgb_delta = 0;
    for (int x = 0; x < normal.getWidth(); ++x)
    {
        for (int y = 0; y < normal.getHeight(); ++y)
        {
            const juce::Colour from_normal = normal.getPixelAt(x, y);
            const juce::Colour from_ghost = ghost.getPixelAt(x, y);
            largest_alpha_drop =
                std::max(largest_alpha_drop, from_normal.getAlpha() - from_ghost.getAlpha());
            if (from_normal.getAlpha() == 255 && from_ghost.getAlpha() == 127)
            {
                ++isolated_ink_pixels;
                worst_isolated_rgb_delta = std::max(
                    {worst_isolated_rgb_delta,
                     std::abs(from_normal.getRed() - from_ghost.getRed()),
                     std::abs(from_normal.getGreen() - from_ghost.getGreen()),
                     std::abs(from_normal.getBlue() - from_ghost.getBlue())});
            }
        }
    }
    // A single fully covered group lands at 50% alpha (127 in JUCE's layer compositor). Internal
    // overlap stays at that same alpha because the finished group is faded only once.
    CHECK(largest_alpha_drop == 128);
    CHECK(isolated_ink_pixels > 0);
    // Premultiplying at 50% alpha and recovering straight RGB costs at most two 8-bit counts.
    CHECK(worst_isolated_rgb_delta <= 2);

    // The plain fret digit is redrawn opaquely above the translucent group.
    const int onset_x = juce::roundToInt(metrics.x(5.0));
    const int center_y = juce::roundToInt(metrics.laneY(3));
    const int normal_digit_top = topDigitInkRow(normal, onset_x, center_y);
    CHECK(normal_digit_top > 0);
    CHECK(topDigitInkRow(ghost, onset_x, center_y) == normal_digit_top);

    // A dead note's white plate lands at 0.75 over the 0.5 note beneath it: JUCE's integer
    // source-over produces alpha 222. The grouped white X remains at 127, while the plate is no
    // longer opaque at 255.
    const int head_half = juce::roundToInt(metrics.headSize() / 2.0f);
    int strongest_ghost_plate_alpha = 0;
    for (int x = onset_x - head_half; x <= onset_x + head_half; ++x)
    {
        for (int y = center_y - head_half; y <= center_y + head_half; ++y)
        {
            if (isWhiteInk(muted_normal.getPixelAt(x, y)))
            {
                const juce::Colour ghost_pixel = muted_ghost.getPixelAt(x, y);
                if (ghost_pixel.getRed() >= 200 && ghost_pixel.getGreen() >= 200 &&
                    ghost_pixel.getBlue() >= 200 && ghost_pixel.getAlpha() < 250)
                {
                    strongest_ghost_plate_alpha = std::max(
                        strongest_ghost_plate_alpha, static_cast<int>(ghost_pixel.getAlpha()));
                }
            }
        }
    }
    CHECK(strongest_ghost_plate_alpha == 222);

    // The head is opaque while the note group is assembled, so its right half lands identically
    // over its own tail and its left half over bare lane. Per-ink alpha breaks this identity by
    // revealing the tail only on the right.
    const int offset = juce::roundToInt(metrics.headSize() * 0.32f);
    const juce::Colour over_tail = ghost.getPixelAt(onset_x + offset, center_y);
    const juce::Colour over_lane = ghost.getPixelAt(onset_x - offset, center_y);
    CAPTURE(over_tail.toString(), over_lane.toString(), onset_x, center_y, offset);
    CHECK(over_tail == over_lane);
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
    const auto painted = [&metrics](const common::core::NoteViewState& note) {
        common::core::ChartViewState state;
        state.string_count = 6;
        state.notes = {note};
        const std::vector<double> prefix_max = common::core::makeSustainPrefixMax(state.notes);
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

    SECTION("a glide whose last keyframe lands on the sustain end")
    {
        CHECK(mark_reaches(
            painted(
                common::core::NoteViewState{
                    .start_seconds = 5.0,
                    .end_seconds = 9.0,
                    .string = 3,
                    .fret = 5,
                    .bend = {},
                    .slides = {common::core::KeyframeViewState{.seconds = 9.0, .fret = 9}},
                    .vibrato = {},
                }),
            end_x - 1));
    }

    SECTION("a bend's held run after its last bend point")
    {
        CHECK(mark_reaches(
            painted(
                common::core::NoteViewState{
                    .start_seconds = 5.0,
                    .end_seconds = 9.0,
                    .string = 3,
                    .fret = 5,
                    .bend = {common::core::BendPointViewState{.seconds = 7.0, .semitones = 2.0}},
                    .slides = {},
                    .vibrato = {},
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
    // Renders one box and probes the text ink, the ground and the frame's presence.
    //
    // The fill's interior lands at full strength, so the ground probes exactly. The GLYPHS do
    // not: how much of an edge pixel a digit covers is the platform text rasterizer's business,
    // and CoreText leaves the cores of digits this size a hair under full coverage, so exact
    // equality counted one pixel on macOS where Windows counted plenty. A painted pixel is
    // therefore classified to whichever of the two KNOWN colors it lies nearer -- the box has
    // only these two, so nearer-to-ink is exactly 'this pixel is glyph'. That needs no
    // tolerance to pick, and it cannot drift: every pixel that matched exactly still classifies
    // as ink. Unpainted pixels are neither and are skipped.
    //
    // The one-pixel frame sits on fractional edges and antialiases everywhere, so it probes by
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
                text_pixels += pixel.getAlpha() > 0 && nearerTo(pixel, ink, ground) ? 1 : 0;
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
    const auto painted_box = [&border](const common::core::NoteViewState* note) {
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
    const common::core::NoteViewState scrape{
        .start_seconds = 5.0,
        .end_seconds = 6.0,
        .string = 3,
        .fret = 9,
        .attack = common::core::NoteAttack::PickSlide,
        .legato = common::core::LegatoMotion::Unjustified,
        .bend = {},
        .slides = {},
        .vibrato = {},
    };
    const int plain_top = top_ink_row(painted_box(nullptr), juce::Colour{0xffffffff});
    const int scrape_top = top_ink_row(painted_box(&scrape), juce::Colour{0xffffffff});
    REQUIRE(plain_top > 0);
    REQUIRE(scrape_top > 0);
    CHECK(scrape_top < plain_top);
}

// The drawn-note accessor is the seam a host composing two forms of one chart draws through (the
// editor's actual-ring pick), so the core must take the note it is handed AT EACH INDEX rather
// than the state's own. Checked as an image identity against the state that composition names,
// with one note from each form: a core reading the accessor once, or not at all, cannot match a
// picture that is half one form and half the other.
TEST_CASE("Tab paint core draws the note the drawn-note accessor picks", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // Two notes on their own lanes at one onset, differing only in how far their tails run.
    const auto state_with = [](double upper_end, double lower_end) {
        common::core::ChartViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::NoteViewState{
                .start_seconds = 5.0,
                .end_seconds = upper_end,
                .string = 3,
                .fret = 7,
                .bend = {},
                .slides = {},
                .vibrato = {},
            },
            common::core::NoteViewState{
                .start_seconds = 5.0,
                .end_seconds = lower_end,
                .string = 4,
                .fret = 5,
                .bend = {},
                .slides = {},
                .vibrato = {},
            },
        };
        return state;
    };
    const common::core::ChartViewState presented = state_with(6.0, 6.0);
    const common::core::ChartViewState actual = state_with(9.0, 9.0);
    const common::core::ChartViewState mixed = state_with(9.0, 6.0);

    const auto painted = [](const common::core::ChartViewState& tab,
                            const std::vector<double>& prefix_max,
                            const TabDrawnNote& drawn_note) {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(
            graphics,
            referenceMetrics(tab.string_count),
            tab,
            prefix_max,
            common::core::makeSustainPrefixMax(tab.shapes),
            drawn_note);
        return image;
    };

    // The longest form's ends, which bound every picture below: the window spans all of them, so
    // the same table serves each render and only the drawn notes differ.
    const std::vector<double> reach = common::core::makeSustainPrefixMax(actual.notes);
    const juce::Image composed = painted(
        presented,
        reach,
        [&presented, &actual](std::size_t index) -> const common::core::NoteViewState& {
            return index == 0 ? actual.notes[index] : presented.notes[index];
        });

    CHECK(worstPixelDelta(composed, painted(mixed, reach, {})) == 0);
    // Not vacuous: neither whole form draws that picture, so the identity above can only hold
    // because the accessor was asked per note.
    CHECK(worstPixelDelta(composed, painted(presented, reach, {})) > 0);
    CHECK(worstPixelDelta(composed, painted(actual, reach, {})) > 0);
}

// [D2]'s amendment 2 on this surface. A landing-opened span states nothing at its landing, so it
// draws no mark there and defers its bracket to the first interior sounding; a span an event
// states keeps its own start. Asked as "which COLUMNS do these renders differ in" rather than by
// probing the bracket's own geometry, so the three pictures are distinguished by where the mark is
// and nothing else — and a bracket-less span is the common ground all three are measured against.
TEST_CASE("Tab paint core draws a deferred bracket where the sound is", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // One arpeggio span and nothing else: the rails are identical in every render, so any column
    // the renders disagree in is the bracket's.
    const auto state_marked_at = [](std::optional<double> bracket_seconds) {
        common::core::ChartViewState state;
        state.string_count = 6;
        state.shapes = {
            common::core::ShapeViewState{
                .start_seconds = 10.0,
                .end_seconds = 16.0,
                .arpeggio = true,
                .strings =
                    {common::core::ShapeStringViewState{
                         .string = 3, .fret = 7, .digit = common::core::StopMarkSlot::Bracket
                     },
                     common::core::ShapeStringViewState{
                         .string = 5, .fret = 8, .digit = common::core::StopMarkSlot::Bracket
                     }},
                .bracket_seconds = bracket_seconds,
            },
        };
        return state;
    };
    const auto painted = [](const common::core::ChartViewState& tab) {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(
            graphics,
            referenceMetrics(tab.string_count),
            tab,
            common::core::makeSustainPrefixMax(tab.notes));
        return image;
    };

    // 20 px/s across the 400 px lane: the span's start is column 200 and the deferred mark at
    // 13.0 s is column 260. The two bands are far enough apart that no glyph reaches both.
    const juce::Image at_start = painted(state_marked_at(10.0));
    const juce::Image deferred = painted(state_marked_at(13.0));
    const juce::Image unmarked = painted(state_marked_at(std::nullopt));

    // The control keeps its start: it draws something there that a bracket-less span does not.
    CHECK(worstPixelDeltaInColumns(at_start, unmarked, 190, 215) > 0);
    // The seamless landing: the deferred span draws NOTHING at its own start.
    CHECK(worstPixelDeltaInColumns(deferred, unmarked, 190, 215) == 0);
    // And draws it at the first interior sounding instead, where the control draws nothing.
    CHECK(worstPixelDeltaInColumns(deferred, unmarked, 250, 275) > 0);
    CHECK(worstPixelDeltaInColumns(at_start, unmarked, 250, 275) == 0);
}

// C3 on this surface, under the ruling that made it all-or-nothing per note (user, 2026-08-30): a
// member's ribbon does not draw AT ALL where the covering span's furniture owns its whole ring. The
// head is untouched, so the two renders are compared past the head — where one draws a ribbon over
// the whole ring and the other draws the empty lane. The partial state this once pinned, a ribbon
// starting part way along the ring, is exactly what the ruling deleted; the identity against a lane
// with no note in it is what stands in its place.
TEST_CASE("Tab paint core draws no ribbon for a suppressed tail", "[ui][tab-paint]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    const auto state_suppressing = [](bool tail_suppressed) {
        common::core::ChartViewState state;
        state.string_count = 6;
        state.notes = {
            common::core::NoteViewState{
                .start_seconds = 5.0,
                .end_seconds = 15.0,
                .tail_suppressed = tail_suppressed,
                .string = 3,
                .fret = 7,
                .bend = {},
                .slides = {},
                .vibrato = {},
            },
        };
        return state;
    };
    const auto painted = [](const common::core::ChartViewState& tab) {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        paintTabLane(
            graphics,
            referenceMetrics(tab.string_count),
            tab,
            common::core::makeSustainPrefixMax(tab.notes));
        return image;
    };

    // The ring runs 5.0 s to 15.0 s — columns 100 to 300.
    const juce::Image whole = painted(state_suppressing(false));
    const juce::Image suppressed = painted(state_suppressing(true));

    // The ribbon is in one picture and gone from the other across the WHOLE ring, not a stretch of
    // it: probed near both ends and clear of the head, which draws identically in both.
    CHECK(worstPixelDeltaInColumns(whole, suppressed, 140, 190) > 0);
    CHECK(worstPixelDeltaInColumns(whole, suppressed, 215, 290) > 0);
    // And past its head the suppressed note's picture IS the empty lane's, pixel for pixel: no
    // stub, and above all no ribbon materialising part way along the ring with no head in front of
    // it, which is the sighting the ruling closed.
    common::core::ChartViewState bare;
    bare.string_count = 6;
    CHECK(worstPixelDeltaInColumns(suppressed, painted(bare), 140, 320) == 0);
    // The control that keeps that identity worth asserting: the unsuppressed ribbon plainly is not
    // the empty lane.
    CHECK(worstPixelDeltaInColumns(whole, painted(bare), 140, 320) > 0);
}

} // namespace rock_hero::common::ui
