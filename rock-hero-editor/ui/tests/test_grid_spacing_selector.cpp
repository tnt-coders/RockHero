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

} // namespace

// Verifies preset selection emits the preset note value exactly once.
TEST_CASE("GridSpacingSelector emits chosen presets", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    // Preset id 2 is the quarter-triplet grid: the ladder interleaves triplet subdivisions
    // with the power-of-two values (grid-native authoring, 2026-07-18).
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

// Verifies the default grid displays as 1/4 and entries forward the raw note value unchanged:
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
    CHECK(box.getText() == juce::String{"1/4"});

    box.setText("1/16", juce::sendNotificationSync);

    CHECK(controller.grid_note_value_change_count == 1);
    CHECK(controller.last_grid_note_value == std::optional{common::core::Fraction{1, 16}});
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
