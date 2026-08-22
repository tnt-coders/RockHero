#include "tab/tab_view.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a projection with three notes: a long sustain, a short note inside it, and a late note
// the presentation rules left tail-less — its ACTUAL ring runs a second past its bare head, which
// is the gap the Alt reveal exists to show.
[[nodiscard]] std::shared_ptr<const common::core::ChartViewState> makeTabState()
{
    common::core::ChartViewState state;
    state.string_count = 6;
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 1.0,
            .end_seconds = 9.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 2.5,
            .string = 4,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
    };
    // One entry per note, as the projection guarantees. The first two rings are exactly their
    // presented tails; the last one outlasts a head that draws no tail at all.
    state.actual_end_seconds = {9.0, 2.5, 13.0};
    return std::make_shared<const common::core::ChartViewState>(std::move(state));
}

// Running maximum of the fixture's presented tail ends, matching TabView's internal index.
[[nodiscard]] std::vector<double> prefixMaxEnds()
{
    return {9.0, 9.0, 12.0};
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

// The range query bounds candidates by sorted starts and the prefix maximum of sustain ends.
TEST_CASE("TabView finds notes intersecting a visible span", "[ui][tab-view]")
{
    const auto tab = makeTabState();
    const std::vector<double> prefix_max = prefixMaxEnds();

    // A span in the middle of the long sustain starts the range at that note.
    const auto [mid_first, mid_last] =
        common::core::visibleEventRange(tab->notes, prefix_max, 5.0, 6.0);
    CHECK(mid_first == 0);
    CHECK(mid_last == 2);

    // A span before every note is empty.
    const auto [early_first, early_last] =
        common::core::visibleEventRange(tab->notes, prefix_max, 0.0, 0.5);
    CHECK(early_first == early_last);

    // A span after every sustain is empty.
    const auto [late_first, late_last] =
        common::core::visibleEventRange(tab->notes, prefix_max, 13.0, 14.0);
    CHECK(late_first == late_last);

    // A span across the late zero-length note includes it.
    const auto [end_first, end_last] =
        common::core::visibleEventRange(tab->notes, prefix_max, 11.0, 13.0);
    CHECK(end_first <= 2);
    CHECK(end_last == 3);
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

    view.setState(makeTabState(), 0);
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

// The Alt reveal: while it is held every visible note also outlines the ring the string actually
// sounds for. A note the presentation rules left tail-less shows ink out at its actual end and
// nothing there once the reveal drops; a note whose two ends coincide is outlined all the same,
// over its own tail, which is the statement "this is the whole ring".
TEST_CASE("TabView reveals each note's actual ring while held", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view{};
    view.setBounds(0, 0, 200, 120);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(makeTabState(), 0);

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
    view.setActualRingReveal(true);
    const juce::Image revealed = render();

    // The late note presents no tail, so its ring (12.0s to 13.0s, x = 120 to 130) reaches the
    // screen only through the reveal. Probed on the outline's right edge, well clear of the head,
    // which is 14.3 px wide about x = 120 and so stops at x = 127.
    CHECK(hidden.getPixelAt(129, 12).getARGB() == 0);
    CHECK(revealed.getPixelAt(129, 12).getARGB() != 0);

    // The long sustain's ring and its presented tail coincide, and the outline is drawn anyway —
    // over the tail's own top rail on the bottom lane (string 1, centre y = 110.5, envelope top
    // row 106), so the pixel changes rather than appearing.
    CHECK(hidden.getPixelAt(50, 106).getARGB() != 0);
    CHECK(revealed.getPixelAt(50, 106) != hidden.getPixelAt(50, 106));

    // Releasing snaps back: the reveal is a held state, never a mode that latches.
    view.setActualRingReveal(false);
    const juce::Image released = render();
    CHECK(released.getPixelAt(129, 12).getARGB() == 0);
}

// The reveal culls against its OWN prefix maximum, over the actual ring ends — which is the whole
// reason a second table exists. Here a note's presented tail ends long before the visible window
// while its ring reaches well into it, so the notation's table (which stops at the tails) puts the
// note out of range and only the actual-ends table keeps it. Swap the two tables at the reveal's
// cull and this case is the one that notices.
TEST_CASE("TabView reveals a ring reaching a window its tail cannot", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    common::core::ChartViewState state;
    state.string_count = 6;
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 3.0,
            .string = 6,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
    };
    state.actual_end_seconds = {12.0};

    TabView view{};
    view.setBounds(0, 0, 200, 120);
    // The window opens at 10 s. Widened by the paint core's glyph slack (75 px, here 3.75 s) the
    // visible span still starts at 6.25 s, past the presented end at 3.0 s and far short of the
    // ring's 12.0 s.
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{10.0},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(std::make_shared<const common::core::ChartViewState>(std::move(state)), 0);

    const auto render = [&view] {
        const juce::Image image{juce::SoftwareImageType{}.create(
            juce::Image::ARGB, 200, 120, true)};
        juce::Graphics graphics{image};
        view.paint(graphics);
        return image;
    };

    // 10 seconds across 200 px: 20 px per second, so the ring ends at x = 40 and its outline's
    // right edge fills column 39, on the top lane (string 6, envelope rows 6 through 14). The head
    // sits at x = -160, off the left edge, so nothing but the reveal can put ink there.
    CHECK(render().getPixelAt(39, 12).getARGB() == 0);

    view.setActualRingReveal(true);
    CHECK(render().getPixelAt(39, 12).getARGB() != 0);
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
    view.setState(makeTabState(), 0);

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
    view.setState(makeTabState(), 0);

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

} // namespace rock_hero::editor::ui
