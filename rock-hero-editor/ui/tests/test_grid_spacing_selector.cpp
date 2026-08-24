#include "timeline/grid_spacing_selector.h"

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
