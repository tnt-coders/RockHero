#include "tab/tab_view.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a projection with three notes: a long sustain, a short note inside it, and a late note.
[[nodiscard]] std::shared_ptr<const core::TabViewState> makeTabState()
{
    core::TabViewState state;
    state.string_count = 6;
    state.notes = {
        core::TabNoteView{
            .start_seconds = 1.0,
            .end_seconds = 9.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        core::TabNoteView{
            .start_seconds = 2.0,
            .end_seconds = 2.5,
            .string = 4,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
        core::TabNoteView{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
    };
    return std::make_shared<const core::TabViewState>(std::move(state));
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

// The six highest lanes keep the Rocksmith set anchored at red on the sixth-highest lane, so a
// bass keeps red-orange and extended-range strings extend downward through the tertiary tier.
TEST_CASE("TabView colors strings by their standard-window position", "[ui][tab-view]")
{
    const juce::Colour red = tabStringColor(1, 6);
    const juce::Colour purple = tabStringColor(6, 6);
    CHECK(red == juce::Colour{0xffe23c37});
    CHECK(purple == juce::Colour{0xffaa5fdc});

    // A four-string bass keeps the same low-string colors as a guitar's bottom four.
    CHECK(tabStringColor(1, 4) == red);
    CHECK(tabStringColor(4, 4) == juce::Colour{0xfff08c2d});

    // Extended-range lanes push the standard window up and take tertiary colors below it.
    CHECK(tabStringColor(2, 7) == red);
    CHECK(tabStringColor(1, 7) == juce::Colour{0xff00b5a0});
    CHECK(tabStringColor(1, 8) == juce::Colour{0xffff0090});
    CHECK(tabStringColor(2, 8) == juce::Colour{0xff00b5a0});
    CHECK(tabStringColor(1, 10) == juce::Colour{0xff5854ff});

    // Below indigo the tertiary tier cycles instead of running out.
    CHECK(tabStringColor(1, 11) == juce::Colour{0xff00b5a0});
}

// Standard tablature orientation: the highest string takes the top lane.
TEST_CASE("TabView stacks lanes with the highest string on top", "[ui][tab-view]")
{
    const juce::Rectangle<int> bounds{0, 0, 100, 100};
    CHECK(tabLaneCenterY(2, 2, bounds) == Catch::Approx(25.0f));
    CHECK(tabLaneCenterY(1, 2, bounds) == Catch::Approx(75.0f));
    CHECK(tabLaneCenterY(6, 6, bounds) == Catch::Approx(100.0f / 12.0f));
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

// Painting fills the note head with its string color at the onset position.
TEST_CASE("TabView draws string-colored note heads", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view;
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
    CHECK(image.getPixelAt(3, 110) == juce::Colour{0xffe23c37});

    // Its nine-second sustain tail reaches most of the width at the same lane center.
    CHECK(image.getPixelAt(80, 110).getARGB() != 0);

    // The empty lanes above stay untouched.
    CHECK(image.getPixelAt(10, 30).getARGB() == 0);
}

// Techniques, shape spans, and fret-hand positions all draw without touching empty lanes.
TEST_CASE("TabView draws techniques, shapes, and fret-hand positions", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::TabViewState state;
    state.string_count = 6;
    state.notes = {
        core::TabNoteView{
            .start_seconds = 2.0,
            .end_seconds = 8.0,
            .string = 1,
            .fret = 5,
            .attack = common::core::NoteAttack::Hammer,
            .mute = common::core::NoteMute::Palm,
            .vibrato = true,
            .accent = true,
            .bend = {core::TabBendPointView{.seconds = 4.0, .semitones = 2.0}},
            .slides = {core::TabSlideView{.seconds = 7.0, .fret = 9, .unpitched = false}},
        },
        core::TabNoteView{
            .start_seconds = 3.0,
            .end_seconds = 6.0,
            .string = 2,
            .fret = 12,
            .harmonic = common::core::NoteHarmonic::Natural,
            .tremolo = true,
            .bend = {},
            .slides = {},
        },
        core::TabNoteView{
            .start_seconds = 3.0,
            .end_seconds = 3.0,
            .string = 3,
            .fret = 7,
            .mute = common::core::NoteMute::Full,
            .harmonic = common::core::NoteHarmonic::Pinch,
            .bend = {},
            .slides = {},
        },
    };
    state.shapes = {
        core::TabShapeView{
            .start_seconds = 2.0, .end_seconds = 6.0, .name = "A5", .arpeggio = false
        },
        core::TabShapeView{
            .start_seconds = 10.0, .end_seconds = 12.0, .name = "Dm", .arpeggio = true
        },
    };
    state.fret_hand_positions = {
        core::TabFhpView{.seconds = 2.0, .fret = 5, .width = 4},
    };

    TabView view;
    view.setBounds(0, 0, 400, 240);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        });
    view.setState(std::make_shared<const core::TabViewState>(std::move(state)), 0);

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    view.paint(graphics);

    // The strummed A5 span tints the background inside its range and not outside it.
    CHECK(image.getPixelAt(60, 5).getARGB() != 0);
    CHECK(image.getPixelAt(150, 5).getARGB() == 0);

    // The tremolo strip stays clipped to its sustain: nothing straggles past the note end.
    // String 2 lane of six in 240px: center y = 180. Note ends at 6.0s → x = 120.
    bool tremolo_inside = false;
    for (int x = 62; x < 118; ++x)
    {
        tremolo_inside = tremolo_inside || image.getPixelAt(x, 180).getARGB() != 0;
    }
    CHECK(tremolo_inside);
    // Stop short of x=199 where the Dm arpeggio's antialiased dashed border begins.
    for (int x = 122; x < 198; ++x)
    {
        CHECK(image.getPixelAt(x, 180).getARGB() == 0);
    }

    // The vibrato-and-slide note still anchors its head at the onset on the bottom lane.
    CHECK(image.getPixelAt(33, 220).getARGB() != 0);
}

// A null projection draws nothing and never dereferences missing chart data.
TEST_CASE("TabView draws nothing without a chart", "[ui][tab-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TabView view;
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
