#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>

namespace rock_hero::common::ui
{

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
            .attack = common::core::NoteAttack::Hammer,
            .mute = common::core::NoteMute::Palm,
            .vibrato = true,
            .accent = true,
            .bend = {common::core::TabBendPointView{.seconds = 4.0, .semitones = 2.0}},
            .slides = {common::core::TabSlideView{.seconds = 7.0, .fret = 9, .unpitched = false}},
        },
        common::core::TabNoteView{
            .start_seconds = 3.0,
            .end_seconds = 6.0,
            .string = 2,
            .fret = 12,
            .harmonic = common::core::NoteHarmonic::Natural,
            .tremolo = true,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
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
                common::core::TabArpeggioNoteView{.string = 3, .fret = 7, .sounded = true},
                common::core::TabArpeggioNoteView{.string = 5, .fret = 8, .sounded = false},
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
        tabDisplayedStringCount(state.string_count, 0),
        state.string_count);

    const juce::Image image{juce::SoftwareImageType{}.create(juce::Image::ARGB, 400, 240, true)};
    juce::Graphics graphics{image};
    paintTabLane(graphics, metrics, state, tabPrefixMaxEndSeconds(state.notes));

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
    // clear of string lines, serifs, and text: the unsounded string 5 (lane center y = 60)
    // and the sounded string 3 (y = 140) both wear them.
    CHECK(image.getPixelAt(214, 55).getARGB() != 0);
    CHECK(image.getPixelAt(214, 135).getARGB() != 0);

    // Inside the brackets the head area stays empty (no backing disc), and the sounded string
    // draws no held fret number — its full head comes from the note pass instead.
    CHECK(image.getPixelAt(206, 52).getARGB() == 0);
    CHECK(image.getPixelAt(197, 136).getARGB() == 0);

    // The string line hides between the brackets (probed right of the fret number, left of the
    // bracket's vertical) and resumes past them.
    CHECK(image.getPixelAt(208, 60).getARGB() == 0);
    CHECK(image.getPixelAt(220, 60).getARGB() != 0);

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

} // namespace rock_hero::common::ui
