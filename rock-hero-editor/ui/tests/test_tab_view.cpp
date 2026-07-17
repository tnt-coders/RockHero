#include "tab/tab_view.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a projection with three notes: a long sustain, a short note inside it, and a late note.
[[nodiscard]] std::shared_ptr<const common::core::TabViewState> makeTabState()
{
    common::core::TabViewState state;
    state.string_count = 6;
    state.notes = {
        common::core::TabNoteView{
            .start_seconds = 1.0,
            .end_seconds = 9.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 2.0,
            .end_seconds = 2.5,
            .string = 4,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
    };
    return std::make_shared<const common::core::TabViewState>(std::move(state));
}

// Running maximum of the fixture's note ends, matching TabView's internal index.
[[nodiscard]] std::vector<double> prefixMaxEnds()
{
    return {9.0, 9.0, 12.0};
}

} // namespace

// The chart's string count floors the lane count; a minimum only ever adds lanes.
TEST_CASE("TabView resolves the displayed string count", "[ui][tab-view]")
{
    CHECK(tabDisplayedStringCount(0, 10) == 0);
    CHECK(tabDisplayedStringCount(6, 0) == 6);
    CHECK(tabDisplayedStringCount(6, 10) == 10);
    CHECK(tabDisplayedStringCount(8, 6) == 8);
    CHECK(tabDisplayedStringCount(4, 4) == 4);
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
    // The eighth string takes Charter's near-white gray; the seventh keeps our teal. Eight is the
    // current lane cap (g_max_chart_strings), so no ninth-or-beyond colors are exercised.
    CHECK(tabStringColor(1, 8) == juce::Colour{0xffb6b6b6});
    CHECK(tabStringColor(2, 8) == juce::Colour{0xff00b5a0});
}

// Standard tablature orientation: the highest string takes the top lane. Lanes evenly fill the
// row, and the host sizes the row in proportion to the string count, so the per-lane spacing is
// identical (20px here) whether it is a four-string bass, a six-string guitar, or an eight-string.
TEST_CASE("TabView stacks lanes evenly across a proportional row", "[ui][tab-view]")
{
    // Six strings across the reference-height row: 20px lanes from the top edge down.
    const juce::Rectangle<int> six{0, 0, 100, 120};
    CHECK(tabLaneCenterY(6, 6, six) == Catch::Approx(10.0f));
    CHECK(tabLaneCenterY(1, 6, six) == Catch::Approx(110.0f));

    // Four-string bass: the host shrinks the row to fit, so lanes stay at 20px with no margin.
    const juce::Rectangle<int> bass{0, 0, 100, 80};
    CHECK(tabLaneCenterY(4, 4, bass) == Catch::Approx(10.0f));
    CHECK(tabLaneCenterY(1, 4, bass) == Catch::Approx(70.0f));

    // Eight-string: the host grows the row, so lanes still hold the same 20px spacing.
    const juce::Rectangle<int> eight{0, 0, 100, 160};
    CHECK(tabLaneCenterY(8, 8, eight) == Catch::Approx(10.0f));
    CHECK(tabLaneCenterY(7, 8, eight) == Catch::Approx(30.0f));
    CHECK(tabLaneCenterY(1, 8, eight) == Catch::Approx(150.0f));
}

// The range query bounds candidates by sorted starts and the prefix maximum of sustain ends.
TEST_CASE("TabView finds notes intersecting a visible span", "[ui][tab-view]")
{
    const auto tab = makeTabState();
    const std::vector<double> prefix_max = prefixMaxEnds();

    // A span in the middle of the long sustain starts the range at that note.
    const auto [mid_first, mid_last] = tabVisibleNoteRange(tab->notes, prefix_max, 5.0, 6.0);
    CHECK(mid_first == 0);
    CHECK(mid_last == 2);

    // A span before every note is empty.
    const auto [early_first, early_last] = tabVisibleNoteRange(tab->notes, prefix_max, 0.0, 0.5);
    CHECK(early_first == early_last);

    // A span after every sustain is empty.
    const auto [late_first, late_last] = tabVisibleNoteRange(tab->notes, prefix_max, 13.0, 14.0);
    CHECK(late_first == late_last);

    // A span across the late zero-length note includes it.
    const auto [end_first, end_last] = tabVisibleNoteRange(tab->notes, prefix_max, 11.0, 13.0);
    CHECK(end_first <= 2);
    CHECK(end_last == 3);
}

// Painting draws Charter-style layered note heads on their string lines.
TEST_CASE("TabView draws string-colored note heads", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{30.0});
    TabView view{tempo_map};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(makeTabState(), 0);

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

// The techniques/shapes/FHP pixel coverage moved to the shared paint core's suite
// (rock-hero-common/ui/tests/test_tab_paint_core.cpp) when the drawers were extracted; the
// head-drawing case above stays here as the TabView delegation guard.

// With a chart displayed the lane claims its band and forwards lane-local pointer intents with
// the painted geometry; without one it stays pointer-transparent so seeking is untouched.
TEST_CASE("TabView forwards chart pointer intents when a chart shows", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{30.0});
    TabView view{tempo_map};
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

    view.setState(makeTabState(), 0);
    CHECK(view.wantsPointerAt({50, 60}));
    CHECK(view.hitTest(50, 60));

    const juce::MouseEvent down = testing::makeMouseDownEvent(view, 10.0f, 110.0f);
    view.mouseDown(down);
    REQUIRE(event_count == 1);
    CHECK(last_phase == core::ChartPointerPhase::Down);
    REQUIRE(last_event.has_value());
    CHECK(last_event->x == Catch::Approx(10.0f));
    CHECK(last_event->y == Catch::Approx(110.0f));
    CHECK(last_event->geometry.displayed_count == 6);
    CHECK(last_event->geometry.bounds_width == Catch::Approx(200.0f));
    CHECK(last_event->geometry.visible_timeline.duration().seconds == Catch::Approx(20.0));
    CHECK_FALSE(last_event->modifiers.ctrl);

    view.mouseDrag(testing::makeMouseDragEvent(view, 40.0f, 110.0f, 10.0f, 110.0f));
    CHECK(event_count == 2);
    CHECK(last_phase == core::ChartPointerPhase::Drag);
    CHECK(last_event->x == Catch::Approx(40.0f));

    view.mouseUp(testing::makeMouseDownEvent(view, 40.0f, 110.0f));
    CHECK(event_count == 3);
    CHECK(last_phase == core::ChartPointerPhase::Up);

    // Modifiers travel with the event.
    view.mouseDown(
        testing::makeMouseDownEvent(
            view,
            10.0f,
            110.0f,
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier));
    CHECK(last_event->modifiers.ctrl);
}

// Selection and marquee overlays render above the notation without asserting.
TEST_CASE("TabView renders chart-editing overlays", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{30.0});
    TabView view{tempo_map};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(makeTabState(), 0);
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

// Holding Alt over the lane shows the copy cursor and a snapped ghost of the note a click
// would insert; releasing Alt (or hovering without it) clears both.
TEST_CASE("TabView shows the Alt insert ghost at the snapped position", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{30.0});
    TabView view{tempo_map};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(makeTabState(), 0);

    // Alt-hover at ~12.35s over the string-3 lane (center y = 70): the ghost snaps to the
    // nearest quarter-note grid line at 12.5s (x = 125) — the default map's 120 BPM puts grid
    // lines every 0.5s, and 12.35 is unambiguously nearer 12.5.
    view.mouseMove(
        testing::makeMouseDownEvent(view, 123.0f, 71.0f, juce::ModifierKeys::altModifier));
    CHECK(view.getMouseCursor() == juce::MouseCursor{juce::MouseCursor::CopyingCursor});

    {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        // Probe inside the ghost ring's stroke band above the head center (ring radius ~7.2,
        // stroke 1.5), off the string line so the lane paint cannot satisfy the check.
        CHECK(image.getPixelAt(125, 62).getARGB() != 0);
    }

    // Without Alt the ghost and the copy cursor clear.
    view.mouseMove(testing::makeMouseDownEvent(view, 123.0f, 71.0f, juce::ModifierKeys{}));
    CHECK(view.getMouseCursor() == juce::MouseCursor{juce::MouseCursor::NormalCursor});
    {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        CHECK(image.getPixelAt(120, 62).getARGB() == 0);
    }
}

// A null projection draws nothing and never dereferences missing chart data.
TEST_CASE("TabView draws nothing without a chart", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{30.0});
    TabView view{tempo_map};
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

} // namespace rock_hero::editor::ui
