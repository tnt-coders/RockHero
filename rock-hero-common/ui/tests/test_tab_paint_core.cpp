#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <utility>

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

} // namespace

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
            .mute = common::core::NoteMute::Full,
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
        return common::ui::tabNoteHeadText(note);
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
    // length keeps every head clean: drawNoteTail returns early, so no sustain ribbon and no
    // tremolo strip (which a scrape otherwise rides) reaches the probes.
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
            .mute = common::core::NoteMute::Full,
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
        tabDisplayedStringCount(state.string_count, 0),
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
    paintTabLane(graphics, metrics, state, tabPrefixMaxEndSeconds(state.notes));

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
    // half-chord, which lands a little under the true 24.43 px maximum because it is averaged over
    // a whole pixel row.
    const double scrape_height = columnCoverage(image, scrape_x, lane_y - 20, lane_y + 20);
    const double plain_height = columnCoverage(image, plain_x, lane_y - 20, lane_y + 20);
    CHECK(scrape_height > 25.5);
    CHECK(scrape_height < 26.2);
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

    // NO BOXED NUMBER. A real mute fills its plate with Charter's gray behind the digit; the same
    // probe on the scrape reads the head's own colored center instead.
    CHECK(image.getPixelAt(mute_x + 3, lane_y + 5) == juce::Colour{0xff808080});
    CHECK(image.getPixelAt(scrape_x + 3, lane_y + 5) != juce::Colour{0xff808080});
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
    const auto digitInkClearOfRim = [&image](int center_x, int center_y, int window) {
        int found = 0;
        bool clear = true;
        for (int y = center_y - 20; y <= center_y + 20; ++y)
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
                        clear = clear && near_ink.getAlpha() == 255 &&
                                near_ink != juce::Colour{0xff101010};
                    }
                }
            }
        }
        return std::pair{found, clear};
    };

    const auto [one_digit_ink, one_digit_clear] =
        digitInkClearOfRim(scrape_x, lane_y, g_digit_window);
    CHECK(one_digit_ink > 0);
    CHECK(one_digit_clear);

    // The two-digit scrape sits on string 5 (lane center y = 60). Its window stops short of the
    // chip's own letters so only the fret number answers.
    constexpr int wide_lane_y = 60;
    const auto [two_digit_ink, two_digit_clear] = digitInkClearOfRim(scrape_x, wide_lane_y, 6);
    CHECK(two_digit_ink > one_digit_ink);
    CHECK(two_digit_clear);
    // And it is raised by the same three pixels as the one-digit number.
    const int wide_scrape_digit_top = topDigitInkRow(image, scrape_x, wide_lane_y);
    const int wide_plain_digit_top = topDigitInkRow(image, plain_x, wide_lane_y);
    CHECK(wide_plain_digit_top - wide_scrape_digit_top == 3);
}

} // namespace rock_hero::common::ui
