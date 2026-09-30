#include "shared/editor_theme.h"
#include "tab/tab_view.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/testing/view_state_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a projection with three notes: a long sustain, a short note inside it, and a late note
// the presentation rules left tail-less — its stored ring runs a second past its bare head, which
// is the gap a reveal exists to show. Only that note's ink end differs from its ring end.
[[nodiscard]] std::shared_ptr<const common::core::ChartViewState> makeTabState()
{
    common::core::ChartViewState state;
    state.open_strings = common::core::testing::standardTuning();
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 1.0,
            .ring_end_seconds = 9.0,
            .ink_end_seconds = 9.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 2.5,
            .ink_end_seconds = 2.5,
            .string = 4,
            .fret = 7,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .ring_end_seconds = 13.0,
            .ink_end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };
    return std::make_shared<const common::core::ChartViewState>(std::move(state));
}

// Pushes the fixture, the way the controller publishes it.
void setFixtureState(TabView& view)
{
    view.setState(makeTabState(), 0);
}

// One cropped note on string 3 (centre y = 70.5): struck at 2.0s, inked to 5.0s, ringing to
// `ring_end_seconds`, and carrying `keyframe` past the ink end — a mark only a reveal can show.
[[nodiscard]] std::shared_ptr<const common::core::ChartViewState> makeCroppedKeyframeState(
    const double ring_end_seconds, const common::core::SlideStopViewState& keyframe)
{
    common::core::ChartViewState state;
    state.open_strings = common::core::testing::standardTuning();
    state.notes = {
        common::core::testing::withStops(
            common::core::NoteViewState{
                .start_seconds = 2.0,
                .ring_end_seconds = ring_end_seconds,
                .ink_end_seconds = 5.0,
                .string = 3,
                .fret = 7,
                .bend = {},
                .slides = {},
                .keyframes = {},
                .vibrato = {},
            },
            {keyframe}),
    };
    return std::make_shared<const common::core::ChartViewState>(std::move(state));
}

// The same tuning with no chart events at all: the control every "what does the panel let
// through" case reads against, since the legend still draws (the tuning names its strings) while
// nothing the lane would otherwise put in that column does.
[[nodiscard]] std::shared_ptr<const common::core::ChartViewState> makeEmptyTabState()
{
    common::core::ChartViewState state;
    state.open_strings = common::core::testing::standardTuning();
    return std::make_shared<const common::core::ChartViewState>(std::move(state));
}

// A chart whose FURNITURE is what matters: one hand-shape span covering the whole window, so its
// rails cross the pinned column wherever the panel stands, and two fret-hand placements far enough
// apart that the first governs the left edge long before the second comes near it.
[[nodiscard]] std::shared_ptr<const common::core::ChartViewState> makeFurnitureTabState()
{
    common::core::ChartViewState state;
    state.open_strings = common::core::testing::standardTuning();
    state.shapes = {
        common::core::ShapeViewState{
            .start_seconds = 0.0,
            .drawn_end_seconds = 20.0,
            .close_seconds = 20.0,
            .arpeggio = false,
            .strings = {},
        },
    };
    // A WIDE placement, which spells out its inclusive range ("10-16") and so draws a chip wider
    // than the letters' own panel -- the case that makes "the pinned chip is inert too" say
    // something the panel's own rule does not already say.
    state.fret_hand_positions = {
        common::core::FhpViewState{.seconds = 2.0, .fret = 10, .width = 7},
        common::core::FhpViewState{.seconds = 12.0, .fret = 3, .width = 4},
    };
    return std::make_shared<const common::core::ChartViewState>(std::move(state));
}

// The largest per-channel difference between two renders across a range of COLUMNS, inclusive.
// Whether a column carries a mark is a question about columns, so asking it as "do these two
// renders agree here" needs no knowledge of any mark's own geometry.
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

// Whether any pixel in the window carries `signature`. Every text probe below is a CHANNEL
// RELATIONSHIP rather than a colour match, and this is where they all look for one: whether a
// glyph pixel is ever fully covered is a platform question (CoreText says no at these sizes) while
// the hue of every blend is not, so each caller states the relationship its ink has and no ground
// under it can imitate.
[[nodiscard]] bool anyPixelIn(
    const juce::Image& image, const juce::Rectangle<int> window,
    const std::function<bool(juce::Colour)>& signature)
{
    for (int y = window.getY(); y < window.getBottom(); ++y)
    {
        for (int x = window.getX(); x < window.getRight(); ++x)
        {
            if (signature(image.getPixelAt(x, y)))
            {
                return true;
            }
        }
    }
    return false;
}

// The canvas this lane is transparent over -- in the editor, the waveform row the arrangement view
// paints beneath it. A flat fill stands in for it so "what the panel let through" is one colour to
// compare against.
const juce::Colour g_canvas_ink{0xff203040};

// Renders the view over that canvas, the way the real composition reaches it.
[[nodiscard]] juce::Image renderOverCanvas(TabView& view)
{
    juce::Image image{juce::SoftwareImageType{}.create(
        juce::Image::ARGB, view.getWidth(), view.getHeight(), true)};
    {
        juce::Graphics graphics{image};
        graphics.fillAll(g_canvas_ink);
        view.paint(graphics);
    }
    return image;
}

} // namespace

// The chart's string count floors the lane count; a minimum only ever adds lanes. The rule itself
// is pinned beside its one authority in common/core; this only checks the lane view asks for it.
TEST_CASE("TabView resolves the displayed string count", "[ui][tab-view]")
{
    CHECK(common::core::displayedStringCount(0, 10) == 0);
    CHECK(common::core::displayedStringCount(6, 10) == 10);
    CHECK(common::core::displayedStringCount(8, 6) == 8);
}

// The six highest lanes keep Charter's default set anchored at red on the sixth-highest lane, so
// a bass keeps red-orange and extended-range strings extend downward through our tertiary tier.
TEST_CASE("TabView colors strings by their standard-window position", "[ui][tab-view]")
{
    const juce::Colour red = tabStringColor(1, 6);
    const juce::Colour purple = tabStringColor(6, 6);
    CHECK(red == juce::Colour{0xffed0000});
    CHECK(purple == juce::Colour{0xffd22cf8});

    // A four-string bass keeps the same low-string colors as a guitar's bottom four.
    CHECK(tabStringColor(1, 4) == red);
    CHECK(tabStringColor(4, 4) == juce::Colour{0xffff870a});

    // Extended-range lanes push the standard window up and take tertiary colors below it.
    CHECK(tabStringColor(2, 7) == red);
    CHECK(tabStringColor(1, 7) == juce::Colour{0xff00b5a0});
    // The eighth string takes the achromatic near-white (decided by the rendered magenta trial,
    // plan 45 Q2); the seventh keeps teal. Eight is the current lane cap (g_max_chart_strings),
    // so no ninth-or-beyond colors are exercised.
    CHECK(tabStringColor(1, 8) == juce::Colour{0xffb6b6b6});
    CHECK(tabStringColor(2, 8) == juce::Colour{0xff00b5a0});
}

// Standard tablature orientation: the highest string takes the top lane. Lanes evenly fill the
// row, and the host sizes the row in proportion to the string count, so the per-lane spacing is
// identical (20px here) whether it is a four-string bass, a six-string guitar, or an eight-string.
TEST_CASE("TabView stacks lanes evenly across a proportional row", "[ui][tab-view]")
{
    // Every centre carries the .5 of a pixel ROW CENTRE, which is what lets the marks a lane draws
    // be symmetric about the string line at all: a row spans [N, N+1], so it has a mirror row only
    // when 2 * center_y is whole.
    //
    // Six strings across the reference-height row: 20px lanes from the top edge down.
    const juce::Rectangle<int> six{0, 0, 100, 120};
    CHECK(tabLaneCenterY(6, 6, six) == Catch::Approx(10.5f));
    CHECK(tabLaneCenterY(1, 6, six) == Catch::Approx(110.5f));

    // Four-string bass: the host shrinks the row to fit, so lanes stay at 20px with no margin.
    const juce::Rectangle<int> bass{0, 0, 100, 80};
    CHECK(tabLaneCenterY(4, 4, bass) == Catch::Approx(10.5f));
    CHECK(tabLaneCenterY(1, 4, bass) == Catch::Approx(70.5f));

    // Eight-string: the host grows the row, so lanes still hold the same 20px spacing.
    const juce::Rectangle<int> eight{0, 0, 100, 160};
    CHECK(tabLaneCenterY(8, 8, eight) == Catch::Approx(10.5f));
    CHECK(tabLaneCenterY(7, 8, eight) == Catch::Approx(30.5f));
    CHECK(tabLaneCenterY(1, 8, eight) == Catch::Approx(150.5f));

    // The SPACING is what this case is really about, and the snap leaves it untouched at every
    // count - which is the property that would break if the snap were ever applied per-lane in a
    // way that accumulated rounding.
    CHECK((tabLaneCenterY(1, 6, six) - tabLaneCenterY(6, 6, six)) == Catch::Approx(100.0f));
    CHECK((tabLaneCenterY(1, 4, bass) - tabLaneCenterY(4, 4, bass)) == Catch::Approx(60.0f));
    CHECK((tabLaneCenterY(1, 8, eight) - tabLaneCenterY(8, 8, eight)) == Catch::Approx(140.0f));
}

// Painting draws Charter-style layered note heads on their string lines.
TEST_CASE("TabView draws string-colored note heads", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);
    // The string legend is an OCCLUDER by design: it stands over whatever the notation drew in its
    // pinned column. Park it at the far right so the head probes below read the head, not the
    // chrome over it — the legend's own coverage is what its case asserts.
    view.setVisibleContentLeft(180);

    // A software image keeps pixel readback meaningful: the platform-native image type on
    // Windows is Direct2D-backed and does not rasterize in headless test runs.
    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 200, 120, true)};
    juce::Graphics graphics{image};
    view.paint(graphics);

    // String 1 (low E) sits in the bottom lane of six: center y = 110; onset 1.0s → x = 10.
    // Sample left of center so the fret numeral drawn over the head cannot cover the probe.
    // The head fill is Charter's derivation from the red base: x0.8, brightened, darkened twice.
    CHECK(image.getPixelAt(6, 110) == juce::Colour{0xff7c0000});

    // Its nine-second sustain tail reaches most of the width at the same lane center.
    CHECK(image.getPixelAt(80, 110).getARGB() != 0);

    // The string line runs the full width in the lane color (the red base at 80%).
    CHECK(image.getPixelAt(160, 110) == juce::Colour{0xffbd0000});

    // The space between lanes stays untouched.
    CHECK(image.getPixelAt(10, 20).getARGB() == 0);
}

// THE STRING LEGEND: every string's own pitch name, in that string's own colour, standing on that
// string's line at the window's left edge, over one panel spanning the whole lane.
//
// THE PANEL IS AN EXCLUSION PLUS A TINT, not a scrim laid over finished notation, which is the
// property this case pins. The notation under it is ABSENT rather than quieted, and what the column
// shows instead is the CANVAS -- the waveform this lane is transparent over -- under a tint the
// lane lays before it draws anything of its own. The reader scrolled into a dense passage gets the
// audio back rather than an undecodable ghost of the chart, and the column carries no attenuated
// head at all.
TEST_CASE("TabView excludes the notation from the string legend's column", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);

    const juce::Image charted = renderOverCanvas(view);
    view.setState(makeEmptyTabState(), 0);
    const juce::Image bare = renderOverCanvas(view);
    setFixtureState(view);

    const juce::Rectangle<int> column = view.legendBounds();
    REQUIRE_FALSE(column.isEmpty());

    // NOTHING THE LANE DRAWS REACHES THE COLUMN: the chart and an empty lane are the SAME PICTURE
    // across the panel's columns. Asked as an identity over every mark rather than as a probe on
    // one of them, because the ruling is about the whole content pass.
    CHECK(worstPixelDeltaInColumns(charted, bare, column.getX(), column.getRight() - 1) == 0);

    // And the chart really had ink to lose -- past the column the very same pair disagrees, which
    // is what keeps the identity above from passing on a lane that drew nothing at all.
    CHECK(worstPixelDeltaInColumns(charted, bare, column.getRight(), 199) > 0);

    // THE TINT IS WHAT THE COLUMN SHOWS INSTEAD, laid over the canvas rather than over notation.
    // The probe row sits between two string lines and in the panel's own margin column, so no
    // letter and no line reaches it.
    constexpr int between_lanes_row = 60;
    CHECK(charted.getPixelAt(column.getX(), between_lanes_row) != g_canvas_ink);

    // The one assertion here that moves with the SIGHTING KNOB (g_legend_scrim_opacity): at the
    // shipped translucent setting the column is a real blend — neither the raw canvas (asserted
    // above) nor the unmixed row band. Raise the knob to 1 and this line flips to equality.
    CHECK(
        charted.getPixelAt(column.getX(), between_lanes_row) !=
        editorTheme().waveform_row_background);

    // SCROLLED, the column follows the window's left edge -- which is the whole of "always
    // visible", since the canvas underneath it is what moves during a playback follow. What it
    // leaves behind is the notation exactly as it always drew: string 1's head at x = 10, on the
    // bottom lane of six.
    constexpr int scrolled_pin = 150;
    constexpr int line_row = 110;
    view.setVisibleContentLeft(scrolled_pin);
    const juce::Image scrolled = renderOverCanvas(view);
    CHECK(scrolled.getPixelAt(6, line_row) == juce::Colour{0xff7c0000});

    // And the name is inked in the STRING's own colour in the column's new home, over the tint it
    // stands on rather than over anything the lane drew. The bottom string's colour is a red whose
    // RED runs far ahead of its BLUE, which neither the tint nor any chrome on this lane does.
    const juce::Rectangle<int> scrolled_column = view.legendBounds();
    REQUIRE_FALSE(scrolled_column.isEmpty());
    CHECK(anyPixelIn(
        scrolled,
        juce::Rectangle<int>{scrolled_column.getX(), line_row - 6, scrolled_column.getWidth(), 13},
        [](const juce::Colour pixel) { return pixel.getRed() > pixel.getBlue() + 60; }));
}

// SPAN FURNITURE AND FRET-HAND CHIPS DRAW OVER THE PANEL. A hand shape running under the column is
// still in force there, so a rail cut out of it would say the shape had ended; the panel is a
// current-state column, and what is in force is exactly the state it exists to state. The letters
// stay on top of the furniture, because the one thing the column can never lose is which line is
// which string.
//
// FAILS UNDER PRE-CHANGE CODE: the rails were drawn inside the lane pass, under the opaque scrim,
// so the column showed nothing of them.
TEST_CASE("TabView draws span furniture over the legend column", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(makeFurnitureTabState(), 0);

    const juce::Rectangle<int> column = view.legendBounds();
    REQUIRE_FALSE(column.isEmpty());
    const juce::Image with_span = renderOverCanvas(view);

    // The chord shape's rail runs the lane's top edge for the span's whole duration, and the probe
    // sits inside the panel's own columns -- the stretch a scrim would swallow. Its colour is the
    // shape mark's own authority rather than a literal, so a retune of that palette moves the
    // expectation with it.
    constexpr int rail_row = 1;
    const int inside_panel_x = column.getX() + 1;
    CHECK(with_span.getPixelAt(inside_panel_x, rail_row) == common::ui::tabShapeMarkColor(false));

    // ABSENT WHEN NO SPAN CROSSES IT: the same pixel with no shapes at all carries no rail ink,
    // which is what keeps the check above from passing on any ink that happens to be there. Not
    // pinned to the tint's exact value — that follows the sighting knob.
    view.setState(makeEmptyTabState(), 0);
    const juce::Image without_span = renderOverCanvas(view);
    CHECK(
        without_span.getPixelAt(inside_panel_x, rail_row) != common::ui::tabShapeMarkColor(false));

    // AND THE LETTERS STAY ON TOP OF IT. The top string's name sits on its line at row 10, inside
    // the pinned fret-hand chip's own band (rows 1 to 12) -- the one place a letter and a piece of
    // furniture really do overlap, which is why the probe rows sit strictly INSIDE the chip: a
    // window reaching past its bottom edge would pass on letter ink that never met the chip.
    //
    // The top string's colour is a purple whose BLUE runs far ahead of its GREEN, and nothing the
    // chip draws does -- neither its flat ground nor its near-white digits -- so a pixel with that
    // signature inside the chip is the letter standing on it.
    view.setState(makeFurnitureTabState(), 0);
    view.setVisibleContentLeft(60);
    const juce::Image pinned = renderOverCanvas(view);
    const juce::Rectangle<int> pinned_column = view.legendBounds();
    const juce::Colour top_string_ink = tabStringColor(6, 6);
    REQUIRE(top_string_ink.getBlue() > top_string_ink.getGreen() + 150);
    CHECK(anyPixelIn(
        pinned,
        juce::Rectangle<int>{pinned_column.getX(), 5, pinned_column.getWidth(), 7},
        [](const juce::Colour pixel) { return pixel.getBlue() > pixel.getGreen() + 60; }));
}

// THE GOVERNING FRET-HAND POSITION PINS AT THE LEFT. A placement is a region-scoped value exactly
// like a tempo or a time signature, so the one in force at the view's left edge stands there and
// YIELDS as the next placement's own chip scrolls in -- the timeline ruler's pin law, which both
// rows read from one statement of it (sticky_label.h). The chip is the ORDINARY marker chip, drawn
// through the same authority every scrolling placement draws through and simply given the pin's
// column.
//
// FAILS UNDER PRE-CHANGE CODE: nothing was pinned at all, so a reader scrolled into the middle of
// a song could not tell where the hand was without scrolling back to find the last marker.
TEST_CASE("TabView pins the governing fret-hand position", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(makeFurnitureTabState(), 0);

    // The chip's own ground, and a probe inside it clear of everything else the column carries:
    // the row is the chip's mid-height, so the rounded corners cannot reach it, and the column is
    // two pixels in from the pin -- inside the box, and left of both the chip's centred digits and
    // the legend's centred names.
    const juce::Colour chip_ground{0xff2a2f36};
    constexpr int chip_row = 6;

    // NOTHING PINS BEFORE THE FIRST PLACEMENT. At the canvas's own left edge the song's first
    // placement (2.0s, x = 20) has not arrived, so nothing governs and the column carries only
    // the tint.
    const juce::Rectangle<int> unpinned_column = view.legendBounds();
    REQUIRE_FALSE(unpinned_column.isEmpty());
    const juce::Image unpinned = renderOverCanvas(view);
    CHECK(unpinned.getPixelAt(unpinned_column.getX() + 2, chip_row) != chip_ground);

    // SCROLLED PAST IT, the placement governs the left edge and its chip stands on the panel.
    constexpr int governed_pin = 60;
    view.setVisibleContentLeft(governed_pin);
    const juce::Image governed = renderOverCanvas(view);
    CHECK(governed.getPixelAt(view.legendBounds().getX() + 2, chip_row) == chip_ground);

    // THE PIN YIELDS as the next placement (12.0s, x = 120) comes within a label's clearance of
    // it: the pin is dropped rather than the incoming chip suppressed, so the new value scrolls on
    // to the edge and takes over. Read at the pin's own column, which the dropped chip has left.
    constexpr int yielding_pin = 115;
    view.setVisibleContentLeft(yielding_pin);
    const juce::Image yielded = renderOverCanvas(view);
    CHECK(yielded.getPixelAt(view.legendBounds().getX() + 2, chip_row) != chip_ground);

    // And it yielded TO something: the incoming placement's own chip is drawn at its own column
    // (12.0s maps to x = 120) by the furniture pass, which is what makes the handover a handover
    // instead of a disappearance.
    CHECK(yielded.getPixelAt(121, chip_row) == chip_ground);
}

// THE PINNED CHIP IS INERT CHROME on exactly the terms the letters are, and it is the reason the
// rule is asked of the whole pinned column rather than of the panel alone: a wide placement spells
// out its range, and its chip reaches past the panel's own edge over notation the reader cannot
// see there.
//
// FAILS UNDER PRE-CHANGE CODE, deliberately: with the rule scoped to the legend panel, the strip
// of chip past the panel's right edge answered a press as ordinary lane.
TEST_CASE("TabView answers nothing to a press on the pinned fret-hand chip", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    view.setVisibleTimeline(timeline);

    int event_count = 0;
    std::optional<core::ChartPointerPhase> last_phase;
    view.setPointerEventCallback(
        [&](core::ChartPointerPhase phase, const core::ChartPointerEvent&) {
            last_phase = phase;
            ++event_count;
        });
    view.setState(makeFurnitureTabState(), 0);
    constexpr int governed_pin = 60;
    view.setVisibleContentLeft(governed_pin);

    const juce::Rectangle<int> column = view.legendBounds();
    REQUIRE_FALSE(column.isEmpty());
    const common::ui::TabLaneMetrics metrics =
        common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 200, 120}, timeline, 6, 6);
    const std::shared_ptr<const common::core::ChartViewState> fixture = makeFurnitureTabState();
    const juce::Rectangle<float> chip =
        common::ui::tabFhpChipBounds(metrics, fixture->fret_hand_positions.front(), 0.0f);
    // The fixture's job: a chip WIDER than the letters' panel, or this case would prove nothing
    // the panel's own rule does not already prove.
    REQUIRE(chip.getWidth() > static_cast<float>(column.getWidth()));

    // A column past the panel's right edge but still under the chip, on a row inside the chip.
    const auto under_chip_x = static_cast<float>(column.getRight()) + 1.0f;
    constexpr float chip_y = 3.0f;

    // The lane CLAIMS the pixel like any other in its band -- nothing falls through to the
    // overlay's click-to-seek -- and answers it with nothing.
    CHECK(view.wantsPointerAt({static_cast<int>(under_chip_x), 3}));
    view.mouseDown(testing::makeMouseDownEvent(view, under_chip_x, chip_y));
    CHECK(event_count == 0);

    // Scroll the pin away and the same pixel answers a press again: the rule is the chrome's, not
    // a silenced strip of lane.
    view.setVisibleContentLeft(0);
    view.mouseDown(testing::makeMouseDownEvent(view, under_chip_x, chip_y));
    CHECK(event_count == 1);
    CHECK(last_phase == core::ChartPointerPhase::Down);
}

// THE LEGEND IS INERT CHROME (the pointer half of its ruling): it stands permanently over one
// column of notation, so a press there would select or drag marks the reader cannot see. The lane
// still CLAIMS the column — that is what keeps the press from falling through to the overlay's
// click-to-seek, which would jump the playhead to the leftmost visible time whenever a reader
// clicked a letter — and answers it with nothing.
//
// FAILS UNDER PRE-CHANGE CODE, deliberately: the column was draw-only, so the press went straight
// through to the controller as a Down on hidden notation.
TEST_CASE("TabView answers nothing to a press in the string legend", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    view.setVisibleTimeline(timeline);

    std::optional<core::ChartPointerPhase> last_phase;
    int event_count = 0;
    view.setPointerEventCallback(
        [&](core::ChartPointerPhase phase, const core::ChartPointerEvent&) {
            last_phase = phase;
            ++event_count;
        });
    setFixtureState(view);

    // The column the lane really draws, measured by the paint core rather than guessed here.
    const juce::Rectangle<int> column = common::ui::tabStringLegendBounds(
        common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 200, 120}, timeline, 6, 6),
        common::core::testing::standardTuning(),
        0);
    REQUIRE_FALSE(column.isEmpty());
    const auto legend_x = static_cast<float>(column.getCentreX());
    // Row 110 is string 1's line, where the fixture's first note and its tail are drawn — so the
    // column really is standing over notation a press would otherwise hit.
    constexpr float line_y = 110.0f;

    // The lane claims the column like any other pixel of its band: nothing falls through.
    CHECK(view.wantsPointerAt({column.getCentreX(), 110}));
    CHECK(view.hitTest(column.getCentreX(), 110));

    // And answers it with nothing: no press reaches the controller.
    view.mouseDown(testing::makeMouseDownEvent(view, legend_x, line_y));
    CHECK(event_count == 0);

    // Nor does the right press, which elsewhere on the lane raises the discovery menu.
    std::optional<juce::Point<int>> menu_position;
    view.setContextMenuCallback([&](juce::Point<int> position) { menu_position = position; });
    view.mouseDown(
        testing::makeMouseDownEvent(
            view, legend_x, line_y, juce::ModifierKeys{juce::ModifierKeys::rightButtonModifier}));
    CHECK_FALSE(menu_position.has_value());

    // One pixel past the column the notation answers normally, so the rule is a column and not a
    // silenced lane.
    const auto past_legend_x = static_cast<float>(column.getRight()) + 1.0f;
    view.mouseDown(testing::makeMouseDownEvent(view, past_legend_x, line_y));
    CHECK(event_count == 1);
    CHECK(last_phase == core::ChartPointerPhase::Down);

    // The column travels with the pin, so what it swallows travels too: once it scrolls away, the
    // pixel it covered answers a press again.
    view.setVisibleContentLeft(150);
    view.mouseDown(testing::makeMouseDownEvent(view, legend_x, line_y));
    CHECK(event_count == 2);
    CHECK(last_phase == core::ChartPointerPhase::Down);
}

// The techniques/shapes/FHP pixel coverage moved to the shared paint core's suite
// (rock-hero-common/ui/tests/test_tab_paint_core.cpp) when the drawers were extracted; the
// head-drawing case above stays here as the TabView delegation guard.

// With a chart displayed the lane claims its band and forwards lane-local pointer intents with
// the painted geometry; without one it stays pointer-transparent so seeking is untouched.
TEST_CASE("TabView forwards chart pointer intents when a chart shows", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });

    std::optional<core::ChartPointerPhase> last_phase;
    std::optional<core::ChartPointerEvent> last_event;
    int event_count = 0;
    view.setPointerEventCallback(
        [&](core::ChartPointerPhase phase, const core::ChartPointerEvent& event) {
            last_phase = phase;
            last_event = event;
            ++event_count;
        });

    // Without a chart the lane declines the pointer entirely.
    CHECK_FALSE(view.wantsPointerAt({50, 60}));
    CHECK_FALSE(view.hitTest(50, 60));

    setFixtureState(view);
    // The presses below land on the lane's left edge, which is where the string legend's column
    // stands — and that column answers nothing (it is inert chrome; its own case pins that). Park
    // it at the far right so these read the notation's own gestures.
    view.setVisibleContentLeft(180);
    CHECK(view.wantsPointerAt({50, 60}));
    CHECK(view.hitTest(50, 60));

    const juce::MouseEvent down = testing::makeMouseDownEvent(view, 10.0f, 110.0f);
    view.mouseDown(down);
    REQUIRE(event_count == 1);
    CHECK(last_phase == core::ChartPointerPhase::Down);
    REQUIRE(last_event.has_value());
    if (last_event.has_value())
    {
        CHECK(last_event->x == Catch::Approx(10.0f));
        CHECK(last_event->y == Catch::Approx(110.0f));
        CHECK(last_event->geometry.displayed_count == 6);
        CHECK(last_event->geometry.bounds_width == Catch::Approx(200.0f));
        CHECK(last_event->geometry.visible_timeline.duration().seconds == Catch::Approx(20.0));
        CHECK_FALSE(last_event->modifiers.ctrl);
    }

    view.mouseDrag(testing::makeMouseDragEvent(view, 40.0f, 110.0f, 10.0f, 110.0f));
    CHECK(event_count == 2);
    CHECK(last_phase == core::ChartPointerPhase::Drag);
    REQUIRE(last_event.has_value());
    if (last_event.has_value())
    {
        CHECK(last_event->x == Catch::Approx(40.0f));
    }

    view.mouseUp(testing::makeMouseDownEvent(view, 40.0f, 110.0f));
    CHECK(event_count == 3);
    CHECK(last_phase == core::ChartPointerPhase::Up);

    // The popup gesture raises the discovery menu INSTEAD of a gesture: a right press must never
    // reach the controller as a Down, or it would select, seek, or insert behind the menu.
    std::optional<juce::Point<int>> menu_position;
    view.setContextMenuCallback([&](juce::Point<int> position) { menu_position = position; });
    view.mouseDown(
        testing::makeMouseDownEvent(
            view, 30.0f, 100.0f, juce::ModifierKeys{juce::ModifierKeys::rightButtonModifier}));
    CHECK(event_count == 3);
    REQUIRE(menu_position.has_value());
    if (menu_position.has_value())
    {
        CHECK(menu_position->x == 30);
        CHECK(menu_position->y == 100);
    }

    // With no menu sink installed the gesture is swallowed rather than falling through to a Down.
    view.setContextMenuCallback(nullptr);
    view.mouseDown(
        testing::makeMouseDownEvent(
            view, 30.0f, 100.0f, juce::ModifierKeys{juce::ModifierKeys::rightButtonModifier}));
    CHECK(event_count == 3);

    // Modifiers travel with the event, and a Ctrl-modified LEFT press is the lane's precision
    // gesture on every platform — never the discovery menu. macOS folds Ctrl into JUCE's
    // popup-click modifier, so a popup test that asked isPopupMenu() alone would swallow the
    // precision gesture there and nowhere else; the sink is reinstalled here so this pins that the
    // menu stays shut.
    menu_position.reset();
    view.setContextMenuCallback([&](juce::Point<int> position) { menu_position = position; });
    view.mouseDown(
        testing::makeMouseDownEvent(
            view,
            10.0f,
            110.0f,
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier));
    CHECK(event_count == 4);
    CHECK_FALSE(menu_position.has_value());
    REQUIRE(last_event.has_value());
    if (last_event.has_value())
    {
        CHECK(last_event->modifiers.ctrl);
    }
}

// Selection and marquee overlays render above the notation without asserting.
TEST_CASE("TabView renders chart-editing overlays", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);
    // The selection ring is probed on the head's LEFT band, which is exactly where the string
    // legend's pinned column stands; park the legend at the far right so this case reads the
    // overlay rather than the chrome over it.
    view.setVisibleContentLeft(180);
    view.setEditState(
        core::ChartEditViewState{
            .selected_notes = {0},
            .marquee = core::ChartMarqueeViewState{
                .start_seconds = 3.0,
                .end_seconds = 8.0,
                .top_fraction = 0.25f,
                .bottom_fraction = 0.75f,
            },
        });

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 200, 120, true)};
    juce::Graphics graphics{image};
    view.paint(graphics);

    // The marquee border's top-left corner at (30, 30).
    CHECK(image.getPixelAt(30, 30).getARGB() != 0);

    // The selection highlight rings the first note's head straddling its edge in the theme
    // accent. The thin stroke band leaves no pixel free of edge antialiasing at this scale, so
    // instead of an exact color the probe compares against an unselected render: a band pixel
    // left of the head (head at x = 10, center y = 110, band straddling radius ~7.2) must
    // change when the ring is drawn. This stays valid through stroke-weight tuning.
    view.setEditState(core::ChartEditViewState{});
    const juce::Image plain_image{juce::SoftwareImageType{}.create(
        juce::Image::ARGB, 200, 120, true)};
    juce::Graphics plain_graphics{plain_image};
    view.paint(plain_graphics);
    CHECK(image.getPixelAt(2, 110) != plain_image.getPixelAt(2, 110));
}

// THE REFUSAL FLASH glows around a refused head in the theme's red, lit at the keystroke, and a
// new projection drops it, since its indices name other notes. Probed just outside the head, where
// the glow lies. Its level falls with time, so the probe asks only that the pixel gained red, which
// holds at any level above zero — a slow runner cannot flake it.
TEST_CASE("TabView glows a refused head red until the projection changes", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);
    view.setVisibleContentLeft(180);
    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image.getPixelAt(2, 110);
    };
    const juce::Colour plain = render();

    view.flashRefusal(core::ChartRefusalFlash{.notes = {0}, .keyframes = {}});
    CHECK(render().getRed() > plain.getRed());

    setFixtureState(view);
    CHECK(render() == plain);
}

// A refusal reported again while a flash runs joins it as a UNION: the glow is painted once
// whatever the report count, so a held key's repeats cannot stack into solid red and lose the
// pulse. Asked as a sum over the whole head's box rather than of one pixel, so a second glow shows
// wherever it lands in the halo.
TEST_CASE("TabView glows a refused head once however often it is reported", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const auto render_after = [](const int reports, const juce::Image& image) {
        TabView view{};
        view.setBounds(0, 0, 200, 120);
        view.setVisibleTimeline(
            common::core::TimeRange{
                .start = common::core::TimePosition{},
                .end = common::core::TimePosition{20.0},
            });
        setFixtureState(view);
        view.setVisibleContentLeft(180);
        for (int report = 0; report < reports; ++report)
        {
            view.flashRefusal(core::ChartRefusalFlash{.notes = {0}, .keyframes = {}});
        }
        juce::Graphics graphics{image};
        view.paint(graphics);
    };
    const juce::Image once{juce::SoftwareImageType{}.create(juce::Image::ARGB, 200, 120, true)};
    render_after(1, once);
    const juce::Image many{juce::SoftwareImageType{}.create(juce::Image::ARGB, 200, 120, true)};
    render_after(15, many);
    // The head sits at x = 10, centre y = 110; its ring lies inside this box.
    int red_difference = 0;
    for (int y = 100; y < 120; ++y)
    {
        for (int x = 0; x < 20; ++x)
        {
            red_difference += std::abs(
                static_cast<int>(many.getPixelAt(x, y).getRed()) -
                static_cast<int>(once.getPixelAt(x, y).getRed()));
        }
    }
    // A union renders as one stroke, up to the level's drift over the milliseconds between the
    // two renders; stacked glows saturate the halo.
    CHECK(red_difference <= 40);
}

// The reveal, which is the WHOLE-LANE arm of the drawn-extent pick: while it is held every visible
// note draws to its RING end with nothing selected, so a note the presentation rules left
// tail-less grows a real tail — notation, not an annotation over it. Probed mid-tail on its own
// row rather than at an edge, so an outline around the same span would not pass it.
//
// The lane's state is the only half of the reveal with a headless witness. The editor drives it
// from one predicate — this process is the foreground application AND Alt is physically down —
// whose two halves are both operating-system queries (juce::Process::isForegroundProcess,
// juce::ComponentPeer::getCurrentModifiersRealtime) that no test can set, so the editor side is
// pinned by reading, not by a test.
TEST_CASE("TabView draws each note's actual ring as a tail while held", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);

    // 20 seconds across 200 px, six lanes down 120 px: 10 px per second, and the tail envelope on
    // the TOP lane (string 6, centre y = 10.5) spans rows 6 through 14.
    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    const juce::Image hidden = render();
    view.setRingReveal(true);
    const juce::Image revealed = render();

    // The late note presents no tail, so its ring (12.0s to 13.0s, x = 120 to 130) reaches the
    // screen only through the reveal. Probed INSIDE that span — column 128 is clear of the head,
    // which is 14.3 px wide about x = 120 and so stops at x = 127, and row 12 is inside the tail
    // envelope but off both its rails and the string line at row 10.
    CHECK(hidden.getPixelAt(128, 12).getARGB() == 0);
    CHECK(revealed.getPixelAt(128, 12).getARGB() != 0);

    // A note whose ring and ink end coincide is untouched, because the reveal adds no mark of its
    // own: the long sustain's tail is the same tail either way.
    CHECK(revealed.getPixelAt(50, 110) == hidden.getPixelAt(50, 110));

    // Releasing snaps back: the reveal is a held state, never a mode that latches.
    view.setRingReveal(false);
    CHECK(render().getPixelAt(128, 12).getARGB() == 0);
}

// THE REVEAL IS THE RING, and it reveals a tail hidden for ANY reason because no reason is one of
// its inputs — which is why the two figures here are the two reasons that exist: presentation never
// earned the tail, and the trim cut it short. The CARET PEEK shows either of them one note at a
// time: the caret standing on a note's string inside its stored ring reveals that note and no
// other. (Selecting a note is a ground of its own, pinned further down.)
TEST_CASE("TabView reveals a tail the presentation rules hid", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // A cropped note on string 3 (centre y = 70.5) whose ring runs to 8.0s while its ink stops at
    // 5.0s to clear what follows — so columns 50 to 80 are ring the lane hides behind a tail it
    // does draw. And a chug on the TOP lane (string 6, centre y = 10.5) whose short ring earned no
    // tail at all: a bare head at 12.0s over a string that rings to 13.0s. Onsets ascend, as every
    // projection's notes do.
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 8.0,
            .ink_end_seconds = 5.0,
            .string = 3,
            .fret = 7,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .ring_end_seconds = 13.0,
            .ink_end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };

    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(std::make_shared<const common::core::ChartViewState>(std::move(tab)), 0);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    // Rows 12 and 72 sit the same 1.5 px below their lane centres: inside the tail envelope, off
    // both its rails and off the string line. Column 75 (t = 7.5s) is in the trimmed note's cut
    // stretch, column 30 in the stretch it does draw, and column 128 is inside the chug's ring and
    // clear of its head, which is 14.3 px wide about x = 120 and so stops at x = 127.
    {
        const juce::Image quiet = render();
        CHECK(quiet.getPixelAt(30, 72).getARGB() != 0);
        CHECK(quiet.getPixelAt(75, 72).getARGB() == 0);
        CHECK(quiet.getPixelAt(128, 12).getARGB() == 0);
    }

    // The caret on a note's string inside its stored ring reveals THAT note, whichever of the two
    // hidden tails it stands in — and only that one: the other note's tail stays hidden, the peek
    // being the note's answer rather than the lane's. The square itself is an outline, so neither
    // probe falls on its stroke.
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 12.5, .string = 6},
        });
    {
        const juce::Image with_caret = render();
        CHECK(with_caret.getPixelAt(128, 12).getARGB() != 0);
        CHECK(with_caret.getPixelAt(75, 72).getARGB() == 0);
    }
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 6.0, .string = 3},
        });
    {
        const juce::Image with_caret = render();
        CHECK(with_caret.getPixelAt(75, 72).getARGB() != 0);
        CHECK(with_caret.getPixelAt(128, 12).getARGB() == 0);
    }

    // The reveal shows BOTH, on both lanes at once: it is the lane's answer rather than one note's,
    // and a tail presentation never earned and one the trim cut short are hidden for reasons it
    // does not ask about.
    view.setEditState(core::ChartEditViewState{});
    view.setRingReveal(true);
    {
        const juce::Image revealed = render();
        CHECK(revealed.getPixelAt(128, 12).getARGB() != 0);
        CHECK(revealed.getPixelAt(75, 72).getARGB() != 0);
    }

    // And it stops at the ring the string really sounds: past the RING end there is nothing
    // hidden to show.
    CHECK(render().getPixelAt(85, 72).getARGB() == 0);

    // Releasing snaps back, a held state and never a mode that latches.
    view.setRingReveal(false);
    CHECK(render().getPixelAt(75, 72).getARGB() == 0);
}

// The same rule against the REAL crop, projected by the real derivation rather than assigned into
// a fixture: the two halves are pinned apart (the projection's two ends in test_chart_projection,
// the lane's pick above) and this is the composition, which is where an end the lane never reads
// would hide. A ring reaching its next same-string onset is the everyday case the margin crops.
TEST_CASE("TabView reveals the margin trim the projection derived", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // 4/4 at 120 BPM: a beat is half a second, and the margin is a tenth of a beat.
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // Two beats of ring on string 3, meeting the next onset on its own string exactly — the
        // furthest a stored ring may reach (sustainBoundOf). Presentation stops its ink one margin
        // back to clear that head, so 1.9 to 2.0 beats is ring only the reveal draws.
        common::core::ChartNote{
            .position = common::core::GridPosition{.measure = 1, .beat = 1},
            .string = 3,
            .fret = 7,
            .sustain = common::core::Fraction{2},
            .bend = {},
            .keyframes = {},
        },
        common::core::ChartNote{
            .position = common::core::GridPosition{.measure = 1, .beat = 3},
            .string = 3,
            .fret = 5,
            .sustain = common::core::Fraction{1},
            .bend = {},
            .keyframes = {},
        },
    };
    common::core::Arrangement arrangement;
    arrangement.chart = std::move(chart);

    const common::core::ChartViewState tab =
        common::core::makeChartViewState(arrangement, tempo_map);
    // The fixture is only worth rendering if the derivation really did crop it.
    REQUIRE(tab.notes.size() == 2);
    CHECK(tab.notes[0].ink_end_seconds == Catch::Approx(0.95));
    CHECK(tab.notes[0].ring_end_seconds == Catch::Approx(1.0));

    TabView view{};
    view.setBounds(0, 0, 400, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{2.0},
        });
    view.setState(std::make_shared<const common::core::ChartViewState>(tab), 0);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    // 200 px per second: the drawn tail stops at x = 190 and the ring at x = 200, where the next
    // head stands. That head is 14.3 px wide, so it reaches back only to x = 193 and column 191
    // is cropped-away ring with nothing else over it. Row 72 is 1.5 px below string 3's lane
    // centre.
    CHECK(render().getPixelAt(191, 72).getARGB() == 0);

    // Selecting the note under scrutiny shows it — the selection is a ground of the reveal — and so
    // does standing the caret on its string in the very stretch the crop took: the peek reads the
    // stored ring, not the drawn one.
    view.setEditState(core::ChartEditViewState{.selected_notes = {0}});
    CHECK(render().getPixelAt(191, 72).getARGB() != 0);
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 0.96875, .string = 3},
        });
    CHECK(render().getPixelAt(191, 72).getARGB() != 0);

    // A grid-snapped caret on the ring's END is on the next head, whose onset it is, and not on
    // the ring ending under it: that ring stays cropped.
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 1.0, .string = 3},
        });
    CHECK(render().getPixelAt(191, 72).getARGB() == 0);

    // The lane reveal shows it too, and what either shows is the derivation's own crop.
    view.setEditState(core::ChartEditViewState{});
    view.setRingReveal(true);
    CHECK(render().getPixelAt(191, 72).getARGB() != 0);
}

// THE SPAN ARM of the same reveal. Rule 12a stops a span's rails one margin before the head that
// closed it, and that trim is reachable exactly the way a note's clipped ring is: while the
// whole-lane reveal is held, and while the selection holds a note the span covers. Nothing else
// changes — the rails simply run on to the musical close in the ink they already had, and snap back
// when the ground goes away.
//
// The figure is the projection's own rule-12a case (test_chart_projection), projected by the real
// derivation rather than assigned into a fixture: two strums merging into one span, closed by a
// lone note an eighth after the second. The two halves are pinned apart — the projection's two ends
// there, the lane's pick here — and this is the composition.
TEST_CASE("TabView runs a revealed span's rails to its musical close", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // 4/4 at 120 BPM: a beat is half a second, and the margin is a tenth of a beat.
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
    const auto note = [](const common::core::GridPosition& position,
                         const int string,
                         const int fret,
                         const common::core::Fraction sustain) {
        return common::core::ChartNote{
            .position = position,
            .string = string,
            .fret = fret,
            .sustain = sustain,
            .bend = {},
            .keyframes = {},
        };
    };
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        note(common::core::GridPosition{.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{1}),
        note(common::core::GridPosition{.measure = 1, .beat = 1}, 2, 7, common::core::Fraction{1}),
        note(
            common::core::GridPosition{.measure = 1, .beat = 2},
            1,
            5,
            common::core::Fraction{1, 2}),
        note(
            common::core::GridPosition{.measure = 1, .beat = 2},
            2,
            7,
            common::core::Fraction{1, 2}),
        // The note that CLOSES the span, standing exactly at its musical close.
        note(
            common::core::GridPosition{
                .measure = 1, .beat = 2, .offset = common::core::Fraction{1, 2}
            },
            3,
            7,
            common::core::Fraction{1, 2}),
        // A note the span does not reach at all, for the other half of the selection arm.
        note(common::core::GridPosition{.measure = 2, .beat = 1}, 3, 7, common::core::Fraction{1}),
    };
    common::core::Arrangement arrangement;
    arrangement.chart = std::move(chart);

    const common::core::ChartViewState tab =
        common::core::makeChartViewState(arrangement, tempo_map);
    // The fixture is only worth rendering if the derivation really owed a margin here.
    REQUIRE(tab.shapes.size() == 1);
    CHECK(tab.shapes[0].start_seconds == Catch::Approx(0.0));
    CHECK(tab.shapes[0].drawn_end_seconds == Catch::Approx(0.7));
    CHECK(tab.shapes[0].close_seconds == Catch::Approx(0.75));

    TabView view{};
    view.setBounds(0, 0, 400, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{2.0},
        });
    view.setState(std::make_shared<const common::core::ChartViewState>(tab), 0);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    // 200 px per second: the rails stop at x = 140 and the close stands at x = 150, so column 145
    // is trimmed-away rail and column 155 is past the statement entirely. The top rail occupies
    // rows 0 to 2 of the lane, and row 1 carries nothing else — the topmost string's tail envelope
    // starts several rows below it, and this chart draws no lane chips.
    const auto rail_at = [&render](const int column) {
        return render().getPixelAt(column, 1).getARGB();
    };
    CHECK(rail_at(100) != 0);
    CHECK(rail_at(145) == 0);
    CHECK(rail_at(155) == 0);

    // THE WHOLE-LANE REVEAL: every visible span reads to its close while it is held.
    view.setRingReveal(true);
    CHECK(rail_at(145) != 0);
    // And stops there. The reveal shows the statement's real end, not an unbounded rail.
    CHECK(rail_at(155) == 0);
    // The stretch that always drew is untouched: the reveal EXTENDS the rails rather than moving
    // them, exactly as a revealed note's tail grows out of the tail already on screen.
    CHECK(rail_at(100) != 0);

    // Releasing snaps back: a held state, never a mode that latches.
    view.setRingReveal(false);
    CHECK(rail_at(145) == 0);

    // THE SELECTION ARM: a note the span covers reveals the span it stands in.
    view.setEditState(core::ChartEditViewState{.selected_notes = {0}});
    CHECK(rail_at(145) != 0);

    // The note that CLOSED the span does not, and it is the boundary case that says why: its onset
    // stands AT the close, which is the instant the statement ended rather than an instant inside
    // it, so it is a member of nothing here.
    view.setEditState(core::ChartEditViewState{.selected_notes = {4}});
    CHECK(rail_at(145) == 0);

    // Nor does a selection the span never reaches.
    view.setEditState(core::ChartEditViewState{.selected_notes = {5}});
    CHECK(rail_at(145) == 0);

    // THE CARET ARM: the caret anywhere inside the span's tenure reveals it, with the STRING
    // ignored — a span is lane furniture, not one string's ring — so a caret on the top string
    // still reveals a span whose posture never names it.
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 0.4, .string = 6},
        });
    CHECK(rail_at(145) != 0);

    // Ends-INCLUDED, unlike the selection arm just above: the caret is a position, not a member,
    // and the peek's precedent is that a grid-snapped caret behaves the same wherever it lands —
    // so the same instant that refused the CLOSING NOTE's selection accepts the caret.
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 0.75, .string = 3},
        });
    CHECK(rail_at(145) != 0);

    // Past the close the tenure is over and the caret reveals nothing.
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 0.9, .string = 3},
        });
    CHECK(rail_at(145) == 0);
}

// A ring reaching a window its drawn tail cannot: the note's ink ends long before the visible
// span opens while the ring runs well into it. The lane culls against the RING ends, which is
// what keeps the ring in range — index the lane by the ink ends and the note leaves the range
// before the window opens. Both halves matter: with the reveal off the same conservative
// index still admits the note, and the paint pass must then drop it for ending before the span,
// or the one table would leak a tail the picture does not have.
TEST_CASE("TabView reveals a ring reaching a window its tail cannot", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 12.0,
            .ink_end_seconds = 3.0,
            .string = 6,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };

    TabView view{};
    view.setBounds(0, 0, 200, 120);
    // The window opens at 10 s. Widened by the paint core's glyph slack (75 px, here 3.75 s) the
    // visible span still starts at 6.25 s, past the ink end at 3.0 s and far short of the ring's
    // 12.0 s.
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{10.0},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(std::make_shared<const common::core::ChartViewState>(std::move(tab)), 0);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    // 10 seconds across 200 px: 20 px per second, so the ring ends at x = 40 and column 39 carries
    // its last ink, on the top lane (string 6, envelope rows 6 through 14). The head sits at
    // x = -160, off the left edge, so nothing but the ring can put ink there.
    CHECK(render().getPixelAt(39, 12).getARGB() == 0);

    // The cull runs inside the paint core's own pass, against the one conservative index.
    view.setRingReveal(true);
    CHECK(render().getPixelAt(39, 12).getARGB() != 0);
}

// Every editing overlay traces the note the lane drew, and the ink and the reveal share the head
// exactly: presentation touches only the tail, so a selection ring lands on the head's own pixels
// and a reveal grows only the tail behind it. Checked on one note selected — which reveals it — and
// then also lane-revealed, which must ring the same head either way.
TEST_CASE("TabView keeps its overlays on the head the ink and the reveal share", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    const juce::Image plain = render();
    view.setEditState(core::ChartEditViewState{.selected_notes = {2}});
    const juce::Image selected = render();
    view.setRingReveal(true);
    const juce::Image revealed = render();

    // The accent ring straddles the late note's head edge (head centre x = 120, y = 10.5,
    // half-size ~7.2), so column 112 on the centre row changes when the note is selected — and
    // does not change again when the reveal redraws the lane to the ring ends.
    CHECK(selected.getPixelAt(112, 10) != plain.getPixelAt(112, 10));
    CHECK(revealed.getPixelAt(112, 10) == selected.getPixelAt(112, 10));
}

// THE REVEAL A SELECTION MAKES IS PER NOTE: the selected member of a chord grows the ring
// presentation cropped, while its mate stays at its ink end, and the lane reveal then shows both at
// once. One selected ring never uncrops its neighbours.
TEST_CASE("TabView reveals a selected note and leaves its mate at its ink end", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // A two-note chord at 12 s on the top two lanes, neither drawing a tail; both really ring for
    // a second past it.
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .ring_end_seconds = 13.0,
            .ink_end_seconds = 12.0,
            .string = 6,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .ring_end_seconds = 13.0,
            .ink_end_seconds = 12.0,
            .string = 5,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };

    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(std::make_shared<const common::core::ChartViewState>(std::move(tab)), 0);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    // 20 seconds across 200 px: the rings span x = 120 to 130, and column 128 clears the head,
    // which is 14.3 px wide about x = 120. Six lanes down 120 px put string 6's centre at
    // y = 10.5 and string 5's at y = 30.5, so rows 12 and 32 sit the same 1.5 px inside each
    // tail envelope, off both rails and off the string line.
    constexpr int tail_x = 128;
    constexpr int upper_row = 12;
    constexpr int lower_row = 32;

    // Nothing selected and nothing revealed: presentation cropped both rings off the lane.
    const juce::Image plain = render();
    CHECK(plain.getPixelAt(tail_x, upper_row).getARGB() == 0);
    CHECK(plain.getPixelAt(tail_x, lower_row).getARGB() == 0);

    view.setEditState(core::ChartEditViewState{.selected_notes = {0}});
    const juce::Image upper_selected = render();
    CHECK(upper_selected.getPixelAt(tail_x, upper_row).getARGB() != 0);
    CHECK(upper_selected.getPixelAt(tail_x, lower_row).getARGB() == 0);

    view.setEditState(core::ChartEditViewState{.selected_notes = {1}});
    const juce::Image lower_selected = render();
    CHECK(lower_selected.getPixelAt(tail_x, upper_row).getARGB() == 0);
    CHECK(lower_selected.getPixelAt(tail_x, lower_row).getARGB() != 0);

    // The lane reveal covers the whole lane, selection or no selection.
    view.setEditState(core::ChartEditViewState{});
    view.setRingReveal(true);
    const juce::Image revealed = render();
    CHECK(revealed.getPixelAt(tail_x, upper_row).getARGB() != 0);
    CHECK(revealed.getPixelAt(tail_x, lower_row).getARGB() != 0);
}

// THE SELECTION REVEALS WHAT IT NAMES. A cropped note's keyframe past the ink end is undrawn until
// its note is revealed, and a selection is one of the grounds: the note's own, or its keyframe's
// alone, since a selected mark must have a ring to show it. Either draws the note to its ring end
// with the mark at the instant the chart stores, exactly as the lane reveal does, and the accent
// ring traces the mark there rather than the crop; deselecting crops it straight back. Checked on
// both kinds of mark past the ink: an end statement's chip and a linked keyframe's head.
TEST_CASE(
    "TabView reveals a selected note or keyframe and rings the mark at its instant",
    "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(timeline);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    // Shows the cropped note carrying `keyframe`, and answers the mark's layout at the instant the
    // chart stores — where the revealed note draws it — from the manifest the lane draws with, so
    // each probe is the mark's own extent rather than a guessed column.
    const auto show = [&view, &timeline](
                          const double ring_end_seconds,
                          const common::core::SlideStopViewState& keyframe) {
        const std::shared_ptr<const common::core::ChartViewState> state =
            makeCroppedKeyframeState(ring_end_seconds, keyframe);
        view.setState(state, 0);
        return common::ui::tabKeyframeLayout(
            common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 200, 120}, timeline, 6, 6),
            state->notes[0],
            state->notes[0].keyframes.front(),
            ring_end_seconds,
            true);
    };

    // Whether two renders differ over the mark's box, grown by a few pixels so the probe also
    // takes in the accent ring straddling its edge.
    const auto differs_at = [](const common::ui::TabKeyframeLayout& mark,
                               const juce::Image& lhs,
                               const juce::Image& rhs) {
        constexpr int ring_margin = 3;
        for (int y = juce::roundToInt(mark.box.y) - ring_margin;
             y < juce::roundToInt(mark.box.y + mark.box.height) + ring_margin;
             ++y)
        {
            for (int x = juce::roundToInt(mark.box.x) - ring_margin;
                 x < juce::roundToInt(mark.box.x + mark.box.width) + ring_margin;
                 ++x)
            {
                if (lhs.getPixelAt(x, y) != rhs.getPixelAt(x, y))
                {
                    return true;
                }
            }
        }
        return false;
    };

    // The note's one keyframe, as a selection names it.
    const core::ChartKeyframeRef its_keyframe{.note_index = 0, .keyframe_index = 0};

    SECTION("an end statement's chip, selected with its note, stands at its stored instant")
    {
        // The slide-out stated at the stored ring end, 8.0s, three seconds past the ink.
        const common::core::SlideStopViewState stored_end{
            .seconds = 8.0, .fret = 9, .slide_out = true
        };
        const common::ui::TabKeyframeLayout chip = show(8.0, stored_end);
        REQUIRE(chip.shape == common::ui::TabKeyframeShape::Chip);

        // Unrevealed and unselected, the chip past the ink end is undrawn at its instant.
        const juce::Image plain = render();

        // The lane reveal draws it, at the instant the chart stores ...
        view.setRingReveal(true);
        const juce::Image revealed = render();
        CHECK(differs_at(chip, revealed, plain));

        // ... and so does selecting the statement — what clicking its chip publishes — with the
        // lane reveal released: the selection reveals the note, and rings the chip there.
        view.setRingReveal(false);
        view.setEditState(
            core::ChartEditViewState{.selected_notes = {0}, .selected_keyframes = {its_keyframe}});
        const juce::Image selected = render();
        CHECK(differs_at(chip, selected, plain));
        CHECK(differs_at(chip, selected, revealed));

        // Deselecting crops the note back: the stretch past the ink end is as it was.
        view.setEditState(core::ChartEditViewState{});
        CHECK_FALSE(differs_at(chip, render(), plain));
    }

    SECTION("a linked keyframe's head, selected alone, still reveals its note")
    {
        // A glide rising 7 -> 9 at 10.0s (x = 100), inside a ring running on to 12.0s.
        const common::core::SlideStopViewState glide{
            .seconds = 10.0, .fret = 9, .slide_out = false
        };
        const common::ui::TabKeyframeLayout head = show(12.0, glide);
        REQUIRE(head.shape == common::ui::TabKeyframeShape::Head);

        // 10 px per second: column 115 (11.5s) is ring past the keyframe's head and short of the
        // ring end at x = 120, and row 72 sits 1.5 px below the lane centre, inside the tail
        // envelope and off both its rails and the string line.
        constexpr int ring_x = 115;
        constexpr int tail_row = 72;

        // Nothing selected: the tail stops at the ink end, and nothing stands at the keyframe.
        const juce::Image plain = render();
        CHECK(plain.getPixelAt(ring_x, tail_row).getARGB() == 0);

        // The NOTE selected: drawn to its ring end, the keyframe's head at its true instant.
        view.setEditState(core::ChartEditViewState{.selected_notes = {0}});
        const juce::Image note_selected = render();
        CHECK(note_selected.getPixelAt(ring_x, tail_row).getARGB() != 0);
        CHECK(differs_at(head, note_selected, plain));

        // Deselected: cropped straight back, the whole picture as it was.
        view.setEditState(core::ChartEditViewState{});
        CHECK(worstPixelDeltaInColumns(render(), plain, 0, 199) == 0);

        // The KEYFRAME selected alone: the note is revealed all the same, and the accent ring
        // traces the keyframe's head at its true instant — which the note's own selection does
        // not ring.
        view.setEditState(core::ChartEditViewState{.selected_keyframes = {its_keyframe}});
        const juce::Image keyframe_selected = render();
        CHECK(keyframe_selected.getPixelAt(ring_x, tail_row).getARGB() != 0);
        CHECK(differs_at(head, keyframe_selected, plain));
        CHECK(differs_at(head, keyframe_selected, note_selected));
    }
}

// THE SELECTED OBJECT DRAWS LAST. An arrival stands at the very instant the head it glides into is
// struck at, and the lane paints notes in chart order, so the picked head covers it: a charter who
// selected the arrival would get the accent ring around a mark they cannot read. The overlay
// redraws the mark it is about to ring, through the lane's own drawer, and the probe says so by
// identity — the shared square reads as the arrival's own head, not the picked one's.
TEST_CASE(
    "TabView draws a selected arrival over the head it shares an instant with", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // 400 px across 20 s: the shared instant at 8.0s lands at x = 160, and six lanes down 240 px
    // give a 25 px head — wide enough for a probe eight pixels off centre to clear the fret digit
    // and the accent ring alike, so what it reads is the head FILL.
    const common::core::NoteViewState glide = common::core::testing::withStops(
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 8.0,
            .ink_end_seconds = 8.0,
            .string = 3,
            .fret = 7,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
            .end_head = std::size_t{1},
        },
        {common::core::SlideStopViewState{.seconds = 8.0, .fret = 9, .slide_out = false}});
    const common::core::NoteViewState landing{
        .start_seconds = 8.0,
        .ring_end_seconds = 10.0,
        .ink_end_seconds = 10.0,
        .string = 3,
        .fret = 9,
        .bend = {},
        .slides = {},
        .keyframes = {},
        .vibrato = {},
    };

    TabView view{};
    view.setBounds(0, 0, 400, 240);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    const auto render = [&view](std::vector<common::core::NoteViewState> notes) {
        common::core::ChartViewState state;
        state.open_strings = common::core::testing::standardTuning();
        state.notes = std::move(notes);
        const auto shared_state =
            std::make_shared<const common::core::ChartViewState>(std::move(state));
        view.setState(shared_state, 0);
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    constexpr int probe_x = 168;
    const int probe_y = juce::roundToInt(
        common::ui::tabNoteLayout(
            common::ui::makeTabLaneMetrics(
                juce::Rectangle<int>{0, 0, 400, 240},
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{20.0},
                },
                6,
                6),
            landing)
            .center_y);

    view.setEditState(core::ChartEditViewState{});
    const juce::Image picked_on_top = render({glide, landing});
    // The arrival's own head, with nothing over it: what the shared square must read as once the
    // arrival is the selected object. Alone, its ring ends on no head.
    common::core::NoteViewState lone_glide = glide;
    lone_glide.end_head = std::nullopt;
    const juce::Image arrival_alone = render({lone_glide});

    view.setEditState(
        core::ChartEditViewState{
            .selected_keyframes = {core::ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}},
        });
    const juce::Image arrival_selected = render({glide, landing});

    CHECK(
        arrival_selected.getPixelAt(probe_x, probe_y) !=
        picked_on_top.getPixelAt(probe_x, probe_y));
    CHECK(
        arrival_selected.getPixelAt(probe_x, probe_y) ==
        arrival_alone.getPixelAt(probe_x, probe_y));
}

// A CHIP IS A FACE OF WHAT OWNS IT, so with the caret on it a selected bend point rings its chip
// rather than its dot, and the ring traces the chip AS PAINTED: the chip's click box is as wide as
// the widest amount it can print, and a ring on that box would circle empty lane around a short
// amount. The two faces are rendered against each other — both repaint the chip, so the ring is
// their only difference there — probed as the rows of the chip between the box's edge and the
// painted plate's, which the ring must leave untouched, against the plate's own edge, which it
// must mark.
TEST_CASE("TabView rings a selected bend point's chip on the chip as painted", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // 400 px across 20 s: a half-step point at 10.0s (x = 200) on a note struck at 2.0s.
    const common::core::NoteViewState note{
        .start_seconds = 2.0,
        .ring_end_seconds = 16.0,
        .ink_end_seconds = 16.0,
        .string = 3,
        .fret = 7,
        .bend =
            {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
             common::core::BendPointViewState{.seconds = 10.0, .semitones = 1.0}},
        .slides = {},
        .keyframes = {common::core::KeyframeViewState{
            .seconds = 10.0,
            .offset = common::core::Fraction{8},
            .mark = common::core::KeyframeCurveMark{},
            .bend_point = std::size_t{1},
        }},
        .vibrato = {},
    };
    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    TabView view{};
    view.setBounds(0, 0, 400, 240);
    view.setVisibleTimeline(timeline);
    common::core::ChartViewState state;
    state.open_strings = common::core::testing::standardTuning();
    state.notes = {note};
    view.setState(std::make_shared<const common::core::ChartViewState>(std::move(state)), 0);
    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    const common::ui::TabLaneMetrics metrics =
        common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 400, 240}, timeline, 6, 6);
    const std::optional<common::ui::TabLayoutRect> chip =
        common::ui::tabKeyframeLayout(
            metrics, note, note.keyframes.front(), note.ink_end_seconds, true)
            .bend_chip;
    REQUIRE(chip.has_value());
    if (!chip.has_value())
    {
        return;
    }
    const juce::Image scratch{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics scratch_graphics{scratch};
    const juce::Rectangle<float> plate =
        common::ui::paintTabBendChip(scratch_graphics, metrics, note, 1, *chip);
    // The short amount leaves a margin of box beside the plate wide enough to probe clear of the
    // ring's own stroke.
    REQUIRE(plate.getX() - chip->x > 5.0f);

    const auto render_on = [&view, &render](const core::ChartCaretFace face) {
        view.setEditState(
            core::ChartEditViewState{
                .selected_keyframes = {core::ChartKeyframeRef{
                    .note_index = 0, .keyframe_index = 0
                }},
                .caret = core::ChartCaretViewState{.seconds = 10.0, .string = 3, .face = face},
            });
        return render();
    };
    const juce::Image plain = render_on(core::ChartCaretFace::Mark);
    const juce::Image selected = render_on(core::ChartCaretFace::BendChip);

    const int top = juce::roundToInt(plate.getY());
    const int bottom = juce::roundToInt(plate.getBottom());
    const auto differs_in_columns = [&](const int from_x, const int to_x) {
        for (int y = top; y <= bottom; ++y)
        {
            for (int x = from_x; x <= to_x; ++x)
            {
                if (selected.getPixelAt(x, y) != plain.getPixelAt(x, y))
                {
                    return true;
                }
            }
        }
        return false;
    };
    const int plate_left = juce::roundToInt(plate.getX());
    CHECK(differs_in_columns(plate_left - 1, plate_left + 1));
    CHECK_FALSE(differs_in_columns(juce::roundToInt(chip->x), plate_left - 3));
}

// The selection's bend chips draw OVER the caret square: an onset's chip stands above its head,
// into the square the caret draws around that slot, and the amount must stay readable. Probed
// where the square's top edge crosses the chip's plate, rendered with and without the caret: the
// plate reads the same either way, while the square still shows beside the head.
TEST_CASE("TabView draws a selected note's bend chip over the caret square", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // 400 px across 20 s: a note pre-bent a whole step, struck at 2.0s (x = 40) on string 3.
    const common::core::NoteViewState note{
        .start_seconds = 2.0,
        .ring_end_seconds = 6.0,
        .ink_end_seconds = 6.0,
        .string = 3,
        .fret = 7,
        .bend = {common::core::BendPointViewState{.seconds = 2.0, .semitones = 2.0}},
        .slides = {},
        .keyframes = {},
        .vibrato = {},
    };
    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    TabView view{};
    view.setBounds(0, 0, 400, 240);
    view.setVisibleTimeline(timeline);
    common::core::ChartViewState state;
    state.open_strings = common::core::testing::standardTuning();
    state.notes = {note};
    view.setState(std::make_shared<const common::core::ChartViewState>(std::move(state)), 0);
    const auto render = [&view](
                            const std::optional<core::ChartCaretViewState>& caret,
                            std::vector<std::size_t> selected = {0}) {
        view.setEditState(
            core::ChartEditViewState{.selected_notes = std::move(selected), .caret = caret});
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 400, 240, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    const common::ui::TabLaneMetrics metrics =
        common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 400, 240}, timeline, 6, 6);
    const std::optional<common::ui::TabLayoutRect> chip =
        common::ui::tabNoteLayout(metrics, note).bend_chip;
    REQUIRE(chip.has_value());
    if (!chip.has_value())
    {
        return;
    }
    const juce::Image scratch{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics scratch_graphics{scratch};
    const juce::Rectangle<float> plate =
        common::ui::paintTabBendChip(scratch_graphics, metrics, note, 0, *chip);
    const common::ui::TabLayoutRect square = common::ui::tabSlotHeadSquare(metrics, 2.0, 3);

    const juce::Image without = render(std::nullopt);
    const juce::Image with = render(core::ChartCaretViewState{.seconds = 2.0, .string = 3});
    // The caret is drawn: its square's left edge, at mid-height, beside the head.
    const int square_left = juce::roundToInt(square.x);
    const int square_mid = juce::roundToInt(square.y + square.height / 2.0f);
    REQUIRE(
        with.getPixelAt(square_left, square_mid) != without.getPixelAt(square_left, square_mid));
    // The chip stands on the square's top edge, and the stroke straddling that edge reaches into
    // the plate's last row, so the plate's rows beside the edge are the probe.
    const int edge_y = juce::roundToInt(square.y);
    REQUIRE(static_cast<float>(edge_y - 1) > plate.getY());
    REQUIRE(static_cast<float>(edge_y - 1) < plate.getBottom());
    // Columns inside both the square and the plate, clear of the plate's rounded corners.
    const int from_x = std::max(juce::roundToInt(square.x), juce::roundToInt(plate.getX())) + 3;
    const int to_x =
        std::min(juce::roundToInt(square.x + square.width), juce::roundToInt(plate.getRight())) - 3;
    REQUIRE(from_x < to_x);
    const auto plate_unchanged = [&](const juce::Image& image, const juce::Image& reference) {
        for (int x = from_x; x < to_x; ++x)
        {
            for (int y = edge_y - 1; y <= edge_y + 1; ++y)
            {
                if (static_cast<float>(y) > plate.getY() &&
                    static_cast<float>(y) < plate.getBottom() &&
                    image.getPixelAt(x, y) != reference.getPixelAt(x, y))
                {
                    return false;
                }
            }
        }
        return true;
    };
    CHECK(plate_unchanged(with, without));
    // The probe sees the square where the chip is NOT repainted over it: with nothing selected the
    // same rows change, so the check above is the repaint's doing and not a blind spot.
    CHECK_FALSE(plate_unchanged(
        render(core::ChartCaretViewState{.seconds = 2.0, .string = 3}, {}),
        render(std::nullopt, {})));
}

// The controller-published armed caret renders as a white square outline on its empty slot
// (the marker model); clearing the published caret clears it.
TEST_CASE("TabView renders the empty-slot caret square", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);

    // Caret at 12.5s (x = 125) on string 3 (center y = 70).
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 12.5, .string = 3},
        });
    {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        // Probe inside the square's top stroke band above the slot center (half-size ~7.2,
        // stroke 1.5), off the string line so the lane paint cannot satisfy the check.
        CHECK(image.getPixelAt(125, 62).getARGB() != 0);
        // A shape probe pins the SQUARE: the top edge stays straight out to x = 120 past the
        // slight corner rounding (radius ~1.8), where a circular ring of the same size has
        // already curved well below this row and leaves the pixel empty.
        CHECK(image.getPixelAt(120, 62).getARGB() != 0);
    }

    // No published caret, no square.
    view.setEditState(core::ChartEditViewState{});
    {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        CHECK(image.getPixelAt(125, 62).getARGB() == 0);
    }
}

// The lane pushes the armed caret square's paused-column cut-out span to the track viewport: a
// present y span while a caret is armed, an empty one when it clears, a republish when the row
// layout moves, and a dedup on a change that leaves the span identical.
TEST_CASE("TabView publishes and dedups the caret mask", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    setFixtureState(view);

    // Record every mask handed to the sink so both the pushes and the suppressed no-ops show up.
    std::vector<std::optional<juce::Range<float>>> pushes;
    view.setCaretMaskCallback(
        [&](std::optional<juce::Range<float>> mask) { pushes.push_back(mask); });

    // Installing the sink with no caret armed seeds nothing: the empty mask already matches the
    // viewport's default ungapped column, so the dedup suppresses a redundant push.
    CHECK(pushes.empty());
    CHECK_FALSE(view.caretMaskYRange().has_value());

    // Arming the caret on a slot pushes a present, non-empty cut-out span for the paused column.
    view.setEditState(
        core::ChartEditViewState{
            .caret = core::ChartCaretViewState{.seconds = 12.5, .string = 3},
        });
    REQUIRE(pushes.size() == 1);
    // Bind the pushed optional once: two back() calls are separate expressions to
    // bugprone-unchecked-optional-access, so the has_value check would not guard the dereference.
    const auto& armed_push = pushes.back();
    const auto* const armed_span = armed_push.has_value() ? &*armed_push : nullptr;
    REQUIRE(armed_span != nullptr);
    CHECK(armed_span->getLength() > 0.0f);

    // A horizontal zoom leaves the caret's row unchanged, so its row-fixed y span is identical and
    // the republish dedups instead of handing the viewport a redundant span.
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{10.0},
        });
    CHECK(pushes.size() == 1);

    // Resizing the row moves the string lanes the square rides, so the mask republishes; the dedup
    // only lets a genuinely changed span through, so the extra push proves the span moved.
    view.setBounds(0, 0, 200, 240);
    REQUIRE(pushes.size() == 2);
    CHECK(pushes.back().has_value());

    // Clearing the caret pushes an empty mask so the viewport restores the ungapped column.
    view.setEditState(core::ChartEditViewState{});
    REQUIRE(pushes.size() == 3);
    CHECK_FALSE(pushes.back().has_value());
}

// A null projection draws nothing and never dereferences missing chart data.
TEST_CASE("TabView draws nothing without a chart", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 100, 60);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{10.0},
        });
    view.setState(nullptr, 10);

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 100, 60, true)};
    juce::Graphics graphics{image};
    view.paint(graphics);

    CHECK(image.getPixelAt(50, 30).getARGB() == 0);
}

// A SCROLLING fret-hand chip is the hand row's marker: while the marker plane is open a press on
// it names that placement to the chip sink and never reaches the chart pointer path, so it neither
// seeks nor lands on the string under the chip; a press anywhere else, and any press while the
// plane is closed, still goes to the chart.
TEST_CASE("TabView reports a press on a fret-hand chip as its placement", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    view.setVisibleTimeline(timeline);

    int pointer_events = 0;
    view.setPointerEventCallback(
        [&pointer_events](core::ChartPointerPhase, const core::ChartPointerEvent&) {
            ++pointer_events;
        });
    std::optional<std::size_t> pressed_chip;
    view.setFretHandChipCallback(
        [&pressed_chip](const std::size_t index) { pressed_chip = index; });
    view.setMarkerEditsEnabled(true);
    view.setState(makeFurnitureTabState(), 0);

    // The second placement's chip starts at its own column, 12.0s of a 20s window over 200 pixels,
    // well clear of the legend panel at the window's left.
    const common::ui::TabLaneMetrics metrics =
        common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 200, 120}, timeline, 6, 6);
    const std::shared_ptr<const common::core::ChartViewState> fixture = makeFurnitureTabState();
    const common::core::FhpViewState& second = fixture->fret_hand_positions[1];
    const juce::Rectangle<float> chip =
        common::ui::tabFhpChipBounds(metrics, second, metrics.x(second.seconds));
    REQUIRE_FALSE(chip.isEmpty());

    view.mouseDown(testing::makeMouseDownEvent(view, chip.getCentreX(), chip.getCentreY()));
    CHECK(pressed_chip == std::optional<std::size_t>{1});
    CHECK(pointer_events == 0);

    // Below the chips, the same column is the chart's.
    view.mouseDown(testing::makeMouseDownEvent(view, chip.getCentreX(), 80.0f));
    CHECK(pointer_events == 1);

    // With the marker plane closed there is no selection to make, so the chip press goes to the
    // chart like any other press on the lane, as a ruler chip column seeks then.
    pressed_chip.reset();
    view.setMarkerEditsEnabled(false);
    view.mouseDown(testing::makeMouseDownEvent(view, chip.getCentreX(), chip.getCentreY()));
    CHECK_FALSE(pressed_chip.has_value());
    CHECK(pointer_events == 2);
}

// The selected placement's chip wears the accent outline, and nothing else in the lane changes.
TEST_CASE("TabView outlines the selected fret-hand chip", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    view.setVisibleTimeline(timeline);
    view.setState(makeFurnitureTabState(), 0);
    const juce::Image plain = renderOverCanvas(view);

    view.setEditState(core::ChartEditViewState{.selected_fret_hand_position = 1});
    const juce::Image selected = renderOverCanvas(view);

    const common::ui::TabLaneMetrics metrics =
        common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 200, 120}, timeline, 6, 6);
    const std::shared_ptr<const common::core::ChartViewState> fixture = makeFurnitureTabState();
    const common::core::FhpViewState& second = fixture->fret_hand_positions[1];
    const juce::Rectangle<int> chip =
        common::ui::tabFhpChipBounds(metrics, second, metrics.x(second.seconds))
            .getSmallestIntegerContainer();
    REQUIRE_FALSE(chip.isEmpty());

    CHECK(worstPixelDeltaInColumns(plain, selected, chip.getX(), chip.getRight()) > 0);
    // Well left of the chip the two renders agree.
    CHECK(worstPixelDeltaInColumns(plain, selected, 40, chip.getX() - 10) == 0);
}

// A selected placement the panel PINS is outlined where it is drawn — on the pinned chip — because
// its own column lies under the panel there: selecting it from the keyboard (the hand row's jump
// lands on the placement governing the cursor) must still show which placement is selected.
TEST_CASE("TabView outlines a selected fret-hand chip where it is pinned", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(makeFurnitureTabState(), 0);
    // Scrolled past the first placement (2.0s, x = 20), which then governs the edge and pins.
    view.setVisibleContentLeft(60);
    const juce::Image plain = renderOverCanvas(view);

    view.setEditState(core::ChartEditViewState{.selected_fret_hand_position = 0});
    const juce::Image selected = renderOverCanvas(view);

    const juce::Rectangle<int> panel = view.legendBounds();
    REQUIRE_FALSE(panel.isEmpty());
    CHECK(worstPixelDeltaInColumns(plain, selected, panel.getX(), panel.getX() + 20) > 0);
}

// A placement's pending fret entry wears its box on the placement's chip, the mark whose value the
// digits are typing, and draws nothing anywhere else.
TEST_CASE("TabView draws a fret-hand entry's box on its chip", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    const common::core::TimeRange timeline{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{20.0},
    };
    view.setVisibleTimeline(timeline);
    view.setState(makeFurnitureTabState(), 0);
    const juce::Image plain = renderOverCanvas(view);

    view.setEditState(
        core::ChartEditViewState{
            .pending_fret = core::ChartPendingFretViewState{
                .at = core::ChartPendingFretHandPosition{.index = 1},
                .text = "7",
                .valid = false,
            },
        });
    const juce::Image pending = renderOverCanvas(view);

    const common::ui::TabLaneMetrics metrics =
        common::ui::makeTabLaneMetrics(juce::Rectangle<int>{0, 0, 200, 120}, timeline, 6, 6);
    const std::shared_ptr<const common::core::ChartViewState> fixture = makeFurnitureTabState();
    const common::core::FhpViewState& second = fixture->fret_hand_positions[1];
    const juce::Rectangle<int> chip =
        common::ui::tabFhpChipBounds(metrics, second, metrics.x(second.seconds))
            .getSmallestIntegerContainer();
    REQUIRE_FALSE(chip.isEmpty());

    CHECK(worstPixelDeltaInColumns(plain, pending, chip.getX(), chip.getRight()) > 0);
    CHECK(worstPixelDeltaInColumns(plain, pending, 40, chip.getX() - 20) == 0);
}

} // namespace rock_hero::editor::ui
