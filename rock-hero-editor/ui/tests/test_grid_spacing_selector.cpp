#include "timeline/grid_spacing_selector.h"

#include <array>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

namespace
{

// Records note values emitted by the selector under test.
class RecordingGridListener final : public GridSpacingSelector::Listener
{
public:
    // Stores the emitted note value and counts notifications.
    void onGridNoteValueChosen(common::core::Fraction note_value) override
    {
        last_note_value = note_value;
        chosen_count += 1;
    }

    // Last note value emitted by the selector.
    std::optional<common::core::Fraction> last_note_value{};

    // Number of note-value notifications received.
    int chosen_count{0};
};

// Reports whether two snapshots of the same component agree pixel for pixel over a region.
[[nodiscard]] bool regionIsIdentical(
    const juce::Image& before, const juce::Image& after, juce::Rectangle<int> region)
{
    for (int y = region.getY(); y < region.getBottom(); ++y)
    {
        for (int x = region.getX(); x < region.getRight(); ++x)
        {
            if (before.getPixelAt(x, y) != after.getPixelAt(x, y))
            {
                return false;
            }
        }
    }

    return true;
}

// Smallest rectangle enclosing every pixel that differs between two snapshots, empty when they
// agree everywhere. This is what a state indicator's footprint actually is, so assertions can be
// made about where the indicator is allowed to reach rather than about brightness alone.
[[nodiscard]] juce::Rectangle<int> changedRegion(
    const juce::Image& before, const juce::Image& after)
{
    juce::Rectangle<int> changed;
    for (int y = 0; y < before.getHeight(); ++y)
    {
        for (int x = 0; x < before.getWidth(); ++x)
        {
            if (before.getPixelAt(x, y) != after.getPixelAt(x, y))
            {
                changed = changed.getUnion(juce::Rectangle<int>{x, y, 1, 1});
            }
        }
    }

    return changed;
}

// What a state mark actually laid down: where it landed, and how solidly.
//
// A 1px line drawn at a fractional y spreads over two rows at roughly half coverage each, which is
// visually the quieting treatment rather than a strike. Height alone cannot tell those apart from a
// deliberately soft mark, and colour alone cannot tell them apart from a solid one, so both facts
// travel together.
struct StrikeMeasurement
{
    // Smallest rectangle enclosing every pixel the mark changed.
    juce::Rectangle<int> changed{};

    // Changed pixels that are byte-exactly the mark's own colour.
    int full_coverage_pixels{0};

    // Changed pixels that are some blend of the mark's colour with what was underneath it.
    int partial_coverage_pixels{0};
};

// Measures the mark one snapshot gained over another, given the colour the mark is drawn in.
//
// The first and last columns of the changed region are skipped: the strike spans the value text's
// fitted advance box, whose ends are fractional, so those two columns are legitimately
// part-covered. Every column between them sits over a gap between glyphs, and full coverage there
// is exactly what separates a strikethrough from a veil.
[[nodiscard]] StrikeMeasurement measureStrike(
    const juce::Image& before, const juce::Image& after, juce::Colour ink)
{
    StrikeMeasurement measurement{
        .changed = changedRegion(before, after),
        .full_coverage_pixels = 0,
        .partial_coverage_pixels = 0
    };

    for (int y = measurement.changed.getY(); y < measurement.changed.getBottom(); ++y)
    {
        for (int x = measurement.changed.getX() + 1; x < measurement.changed.getRight() - 1; ++x)
        {
            if (before.getPixelAt(x, y) == after.getPixelAt(x, y))
            {
                continue;
            }

            // A colour's packed ARGB word is its byte-exact identity, so the coverage question is
            // decided on integers and never on component floats.
            if (after.getPixelAt(x, y).getARGB() == ink.getARGB())
            {
                measurement.full_coverage_pixels += 1;
            }
            else
            {
                measurement.partial_coverage_pixels += 1;
            }
        }
    }

    return measurement;
}

} // namespace

// Verifies preset selection emits the preset note value exactly once.
TEST_CASE("GridSpacingSelector emits chosen presets", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    // Preset id 2 is the quarter-triplet grid: the ladder interleaves triplet subdivisions
    // with the power-of-two values (grid-native authoring).
    box.setSelectedId(2, juce::sendNotificationSync);

    CHECK(listener.chosen_count == 1);
    CHECK(listener.last_note_value == std::optional{common::core::Fraction{1, 6}});
}

// Verifies free fraction entry emits exact custom note values, including triplet-style grids.
TEST_CASE("GridSpacingSelector emits free fraction entry", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    box.setText("3/16", juce::sendNotificationSync);
    CHECK(listener.last_note_value == std::optional{common::core::Fraction{3, 16}});

    box.setText("1/12", juce::sendNotificationSync);
    CHECK(listener.last_note_value == std::optional{common::core::Fraction{1, 12}});
    CHECK(listener.chosen_count == 2);
}

// Verifies invalid text reverts to the applied value without emitting a selection.
TEST_CASE("GridSpacingSelector rejects invalid entry", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};
    selector.setNoteValue(common::core::Fraction{1, 8});
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    box.setText("0/4", juce::sendNotificationSync);
    box.setText("garbage", juce::sendNotificationSync);
    box.setText("1/", juce::sendNotificationSync);
    // Digit runs long enough to overflow int parsing are rejected before any conversion runs.
    box.setText("99999999999/4", juce::sendNotificationSync);

    CHECK(listener.chosen_count == 0);
    CHECK(box.getText() == juce::String{"1/8"});
}

// Verifies re-entering the applied value does not emit a redundant selection.
TEST_CASE("GridSpacingSelector ignores unchanged entry", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};
    selector.setNoteValue(common::core::Fraction{1, 4});
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    // "2/8" reduces to the applied 1/4, so nothing musically changes.
    box.setText("2/8", juce::sendNotificationSync);

    CHECK(listener.chosen_count == 0);
    CHECK(box.getText() == juce::String{"1/4"});
}

// Verifies the +/- keyboard step walks the preset ladder in the requested direction, snaps a
// free-entered value to the nearest preset that way, and stays inert (never re-emits, never
// inverts) once no preset lies further in that direction.
TEST_CASE("GridSpacingSelector steps the preset ladder", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};

    // Finer from a preset lands on the next finer preset (1/8 -> 1/12, the interleaved triplet);
    // coarser lands on the next coarser preset (1/8 -> 1/6).
    selector.setNoteValue(common::core::Fraction{1, 8});
    selector.stepNoteValue(1);
    CHECK(listener.last_note_value == std::optional{common::core::Fraction{1, 12}});
    selector.setNoteValue(common::core::Fraction{1, 8});
    selector.stepNoteValue(-1);
    CHECK(listener.last_note_value == std::optional{common::core::Fraction{1, 6}});

    // A free-entered value between presets snaps to the nearest preset in the step direction:
    // 3/16 (between 1/4 and 1/6) steps finer to 1/6 and coarser to 1/4.
    selector.setNoteValue(common::core::Fraction{3, 16});
    selector.stepNoteValue(1);
    CHECK(listener.last_note_value == std::optional{common::core::Fraction{1, 6}});
    selector.setNoteValue(common::core::Fraction{3, 16});
    selector.stepNoteValue(-1);
    CHECK(listener.last_note_value == std::optional{common::core::Fraction{1, 4}});

    // At the ladder ends the step is inert — no re-emit of the current preset.
    const int count_before_ends = listener.chosen_count;
    selector.setNoteValue(common::core::Fraction{1, 128}); // finest preset
    selector.stepNoteValue(1);                             // no finer preset exists
    selector.setNoteValue(common::core::Fraction{1, 4});   // coarsest preset
    selector.stepNoteValue(-1);                            // no coarser preset exists
    CHECK(listener.chosen_count == count_before_ends);

    // A free-entered value past a ladder end never snaps back against the step direction.
    selector.setNoteValue(common::core::Fraction{1, 256}); // finer than every preset
    selector.stepNoteValue(1);                             // inert (would have inverted to 1/128)
    selector.setNoteValue(common::core::Fraction{1, 2});   // coarser than every preset
    selector.stepNoteValue(-1);                            // inert (would have inverted to 1/4)
    CHECK(listener.chosen_count == count_before_ends);
}

// Verifies the snap-off indicator marks the value and only the value: a mark appears within the
// readout's value text, the caption strip and the drop-down arrow end render identically in both
// states, and snapping back restores the readout exactly. The region identity is the point rather
// than a brightness sample — a veil over the control or a strike run across the arrow both make
// the grid look unavailable when it is still fully selectable, and both are what these assertions
// forbid.
TEST_CASE("GridSpacingSelector strikes only the value while snap is off", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};
    selector.setNoteValue(common::core::Fraction{1, 16});
    selector.setBounds(0, 0, 220, 24);
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    const auto snapshot = [&selector]() {
        return selector.createComponentSnapshot(selector.getLocalBounds());
    };

    // Everything left of the readout is the "Grid" caption; the right third of the readout carries
    // the drop-down arrow. Both are derived from the box's placed bounds so the probe follows the
    // layout instead of restating it.
    const juce::Rectangle<int> caption_region = selector.getLocalBounds().withRight(box.getX());
    const juce::Rectangle<int> arrow_region =
        box.getBounds().withLeft(box.getRight() - (box.getWidth() / 3));

    const juce::Image snapped = snapshot();
    selector.setSnapEnabled(false);
    const juce::Image unsnapped = snapshot();

    // The mark exists, lands inside the readout, and is narrow enough to be the value's own
    // digits rather than a stroke across the control.
    const juce::Rectangle<int> changed = changedRegion(snapped, unsnapped);
    CHECK_FALSE(changed.isEmpty());
    CHECK(box.getBounds().contains(changed));
    CHECK(changed.getWidth() < box.getWidth() / 2);

    // Nothing outside the value moves.
    CHECK(regionIsIdentical(snapped, unsnapped, caption_region));
    CHECK(regionIsIdentical(snapped, unsnapped, arrow_region));

    // A state, not a disable: the grid stays selectable while the indicator shows.
    CHECK(box.isEnabled());

    // Snapping back leaves no trace anywhere, the struck value included.
    selector.setSnapEnabled(true);
    CHECK(regionIsIdentical(snapped, snapshot(), selector.getLocalBounds()));
}

// Verifies the snap-off mark is a strikethrough rather than a dimming, and that it is drawn on the
// same row whatever the value reads.
//
// Both assertions are falsifiers for a specific way this mark degrades. The digit band's centre is
// fractional, so a 1px line laid there covers two rows at about half strength each — measurably
// the quieted look that sighting rejected, reached by accident rather than by choice; only a
// whole-pixel row gives one row of the digits' own ink. And a row derived from the value rather
// than from the font's line box would drift as the number got wider, drawing one fixed state as a
// varying mark, which is exactly the defect the replaced diagonal had.
TEST_CASE("GridSpacingSelector strikes one full-coverage row at every value", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};

    // The transport strip's own control height, because a mark that has to land on a whole pixel
    // row is only proven at the size it actually ships at.
    selector.setBounds(0, 0, 220, 32);
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    // The strike is drawn in the digits' own ink, so the box is the single place both it and this
    // assertion read that colour from; a theme colour that merely matches would have the test and
    // the paint agreeing by hand.
    const juce::Colour ink = box.findColour(juce::ComboBox::textColourId);

    // The narrowest and the widest values the presets offer, with the default in between: the
    // strike's row must not notice the difference.
    const std::array<common::core::Fraction, 3> values{
        common::core::Fraction{1, 4},
        common::core::Fraction{1, 16},
        common::core::Fraction{1, 128},
    };

    std::optional<int> first_row;
    for (const common::core::Fraction value : values)
    {
        // Driven through the control's own API, so the mark is measured against the value the
        // component really displays.
        selector.setSnapEnabled(true);
        selector.setNoteValue(value);
        const juce::Image snapped = selector.createComponentSnapshot(selector.getLocalBounds());
        selector.setSnapEnabled(false);
        const juce::Image unsnapped = selector.createComponentSnapshot(selector.getLocalBounds());

        const StrikeMeasurement measurement = measureStrike(snapped, unsnapped, ink);

        // One row, fully inked. Two half-covered rows would be the dimming, not the strike.
        CHECK(measurement.changed.getHeight() == 1);
        CHECK(measurement.partial_coverage_pixels == 0);
        CHECK(measurement.full_coverage_pixels > 0);

        // The same row at every value.
        if (!first_row.has_value())
        {
            first_row = measurement.changed.getY();
        }
        CHECK(first_row == std::optional{measurement.changed.getY()});
    }
}

// Verifies the default grid displays as 1/16 and entries forward the raw note value unchanged:
// the note value is the product-wide grid unit, so the view performs no conversion.
TEST_CASE("EditorView grid selector forwards note values", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(4.0));

    auto& box = findRequiredDescendant<juce::ComboBox>(view, "grid_note_value_box");
    CHECK(box.getText() == juce::String{"1/16"});

    box.setText("1/8", juce::sendNotificationSync);

    CHECK(controller.grid_note_value_change_count == 1);
    CHECK(controller.last_grid_note_value == std::optional{common::core::Fraction{1, 8}});
}

// Verifies a note value pushed through view state is displayed verbatim.
TEST_CASE("EditorView grid selector displays the state note value", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    auto state = makeLoadedEditorState(4.0);
    state.grid_note_value = common::core::Fraction{1, 8};
    view.setState(state);

    auto& box = findRequiredDescendant<juce::ComboBox>(view, "grid_note_value_box");
    CHECK(box.getText() == juce::String{"1/8"});
}

} // namespace rock_hero::editor::ui
