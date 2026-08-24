#include "timeline/grid_spacing_selector.h"

#include <algorithm>
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

// Verifies the snap-off indicator on the readout: the control is veiled and a strike crosses the
// value, and both leave again when snap returns. The two marks say different things, so both are
// pinned — the veil alone would be indistinguishable from a disabled control, which is why the
// strike is sampled at the middle of the box, past the left-aligned value text where nothing else
// is ever drawn.
TEST_CASE("GridSpacingSelector marks the readout while snap is off", "[ui][grid-spacing]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingGridListener listener;
    GridSpacingSelector selector{listener};
    selector.setNoteValue(common::core::Fraction{1, 16});
    selector.setBounds(0, 0, 220, 24);
    auto& box = findRequiredDescendant<juce::ComboBox>(selector, "grid_note_value_box");

    // Mean brightness over the combo box's own opaque chrome: the veil can only lower it, and one
    // hairline strike is far too thin to lift it back.
    const auto box_mean_brightness = [&selector, &box]() {
        const juce::Image image = selector.createComponentSnapshot(selector.getLocalBounds());
        const juce::Rectangle<int> bounds = box.getBounds();
        float total = 0.0f;
        for (int x = bounds.getX(); x < bounds.getRight(); ++x)
        {
            for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
            {
                total += image.getPixelAt(x, y).getBrightness();
            }
        }

        return total / static_cast<float>(std::max(1, bounds.getWidth() * bounds.getHeight()));
    };

    // Brightest pixel in a small window at the box's centre, which the diagonal crosses exactly.
    const auto centre_peak_brightness = [&selector, &box]() {
        const juce::Image image = selector.createComponentSnapshot(selector.getLocalBounds());
        const juce::Rectangle<int> bounds = box.getBounds();
        float peak = 0.0f;
        for (int x = bounds.getCentreX() - 2; x <= bounds.getCentreX() + 2; ++x)
        {
            for (int y = bounds.getCentreY() - 2; y <= bounds.getCentreY() + 2; ++y)
            {
                peak = std::max(peak, image.getPixelAt(x, y).getBrightness());
            }
        }

        return peak;
    };

    const float snapped_mean = box_mean_brightness();
    const float snapped_peak = centre_peak_brightness();

    selector.setSnapEnabled(false);
    CHECK(box_mean_brightness() < snapped_mean);
    CHECK(centre_peak_brightness() > snapped_peak);
    // A state, not a disable: the grid stays selectable while the indicator shows.
    CHECK(box.isEnabled());

    selector.setSnapEnabled(true);
    CHECK(box_mean_brightness() == Catch::Approx(snapped_mean));
    CHECK(centre_peak_brightness() == Catch::Approx(snapped_peak));
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
