#include "keybinds/actions_window.h"
#include "keybinds/editor_command_registry.h"
#include "keybinds/key_chord_text.h"
#include "keybinds/keymap_editor_view.h"

#include <algorithm>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// The custom editor lists every registry command — grammar verbs included — as a rebindable
// row: enabled chips per binding plus the add affordance.
TEST_CASE("KeymapEditorView builds rows from the registry", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    KeymapEditorView editor{view.commandManager()};
    editor.setSize(560, 520);

    for (const EditorCommandSpec& spec : editorCommandRegistry())
    {
        INFO(spec.name);
        const juce::String id_hex = juce::String::toHexString(toJuceCommandId(spec.id));
        CHECK(findDescendant(editor, "keymap_row_" + id_hex) != nullptr);
        CHECK(findDescendant(editor, "keymap_add_" + id_hex) != nullptr);
        if (!spec.default_keypresses.empty())
        {
            const auto& first_chip =
                findRequiredDescendant<juce::Button>(editor, "keymap_chip_" + id_hex + "_0");
            CHECK(first_chip.isEnabled());
            // Compared through the shared formatter: chip text collapses shifted characters
            // ("?"), so raw getTextDescription would mismatch on layout-resolvable chords.
            CHECK(first_chip.getButtonText() == keyChordText(spec.default_keypresses.front()));
        }
    }
}

// applyBindingChange is the overwrite-and-clear dance: the chord's previous owner loses it,
// exactly one owner remains, and a replace drops the old binding at that index.
TEST_CASE("KeymapEditorView applies binding changes with one owner", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    const juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    const juce::KeyPress f9{juce::KeyPress::F9Key};

    // Adding a new binding.
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f9, {});
    CHECK(
        mappings.findCommandForKeyPress(f9) == toJuceCommandId(EditorCommandId::ToggleUndoHistory));

    // Assigning the same chord elsewhere strips it from the previous owner.
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, f9, {});
    CHECK(mappings.findCommandForKeyPress(f9) == toJuceCommandId(EditorCommandId::TogglePreview3D));
    CHECK_FALSE(
        mappings.getKeyPressesAssignedToCommand(toJuceCommandId(EditorCommandId::ToggleUndoHistory))
            .contains(f9));

    // Replacing binding 0 (F8) swaps it for the new chord.
    const juce::KeyPress f7{juce::KeyPress::F7Key};
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f7, {0});
    CHECK(
        mappings.findCommandForKeyPress(f7) == toJuceCommandId(EditorCommandId::ToggleUndoHistory));
    CHECK(mappings.findCommandForKeyPress(juce::KeyPress{juce::KeyPress::F8Key}) == 0);

    // Removing a binding empties its slot.
    editor.removeBindings(EditorCommandId::ToggleUndoHistory, {0});
    CHECK(mappings.findCommandForKeyPress(f7) == 0);
}

// The trio is rebindable like every other command: it can gain an alias, lose a chord to
// another command through the one-owner dance, and reset back to its registry defaults.
TEST_CASE("KeymapEditorView rebinds and resets the core trio", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    const juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    const juce::KeyPress ctrl_z{'z', juce::ModifierKeys::commandModifier, 0};

    // Undo gains an F10 alias.
    const juce::KeyPress f10{juce::KeyPress::F10Key};
    editor.applyBindingChange(EditorCommandId::Undo, f10, {});
    CHECK(mappings.findCommandForKeyPress(f10) == toJuceCommandId(EditorCommandId::Undo));

    // Another command may take Ctrl+Z through the one-owner dance.
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, ctrl_z, {});
    CHECK(
        mappings.findCommandForKeyPress(ctrl_z) ==
        toJuceCommandId(EditorCommandId::TogglePreview3D));

    // Resetting Undo restores Ctrl+Z (reclaimed from the squatter) and drops the alias.
    editor.resetCommandToDefault(EditorCommandId::Undo);
    CHECK(mappings.findCommandForKeyPress(ctrl_z) == toJuceCommandId(EditorCommandId::Undo));
    CHECK(mappings.findCommandForKeyPress(f10) == 0);
    CHECK_FALSE(
        mappings.getKeyPressesAssignedToCommand(toJuceCommandId(EditorCommandId::TogglePreview3D))
            .contains(ctrl_z));
}

// Per-command reset restores exactly the registry defaults, reclaiming a default chord from
// whichever command took it meanwhile — the one-owner law holds through a reset too.
TEST_CASE("KeymapEditorView resets one command to its defaults", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    const juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    const juce::KeyPress f8{juce::KeyPress::F8Key};
    const juce::KeyPress f9{juce::KeyPress::F9Key};

    // Rebind Undo History off its F8 default, then let another command take F8.
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f9, {0});
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, f8, {});
    CHECK(mappings.findCommandForKeyPress(f8) == toJuceCommandId(EditorCommandId::TogglePreview3D));

    editor.resetCommandToDefault(EditorCommandId::ToggleUndoHistory);

    // The default is restored, the squatter lost it, and the rebind is gone.
    CHECK(
        mappings.findCommandForKeyPress(f8) == toJuceCommandId(EditorCommandId::ToggleUndoHistory));
    CHECK_FALSE(
        mappings.getKeyPressesAssignedToCommand(toJuceCommandId(EditorCommandId::TogglePreview3D))
            .contains(f8));
    CHECK(mappings.findCommandForKeyPress(f9) == 0);
}

// Total rebindability (plan 53 Phase 1b): grammar chords are ordinary bindings now. A command
// may take an arrow key through the one-owner dance, the grammar verb loses it, and resetting
// the verb reclaims it — no chord is refused anymore.
TEST_CASE("KeymapEditorView rebinds grammar chords like any other", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    const juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    const juce::KeyPress right_arrow{juce::KeyPress::rightKey};
    CHECK(
        mappings.findCommandForKeyPress(right_arrow) ==
        toJuceCommandId(EditorCommandId::CaretStepRight));

    editor.applyBindingChange(EditorCommandId::TogglePreview3D, right_arrow, {});
    CHECK(
        mappings.findCommandForKeyPress(right_arrow) ==
        toJuceCommandId(EditorCommandId::TogglePreview3D));
    CHECK_FALSE(
        mappings.getKeyPressesAssignedToCommand(toJuceCommandId(EditorCommandId::CaretStepRight))
            .contains(right_arrow));

    editor.resetCommandToDefault(EditorCommandId::CaretStepRight);
    CHECK(
        mappings.findCommandForKeyPress(right_arrow) ==
        toJuceCommandId(EditorCommandId::CaretStepRight));
}

// Display-equal chords (OS key-shape twins like Shift+'=' and the numpad-arrival '+') render
// as one chip whose operations cover the whole group. Expectations are computed through the
// shared formatter so the assertions hold on any layout, grouped or not.
TEST_CASE("KeymapEditorView groups display-equal chords into one chip", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    editor.setSize(560, 520);
    const juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    const juce::CommandID grid_finer = toJuceCommandId(EditorCommandId::GridFiner);
    const juce::String id_hex = juce::String::toHexString(grid_finer);
    const juce::Array<juce::KeyPress> keys = mappings.getKeyPressesAssignedToCommand(grid_finer);
    REQUIRE(keys.size() >= 2);

    // One chip exists per unique display text, id-tagged with its group's first binding index.
    std::vector<juce::String> seen_texts;
    for (int index = 0; index < keys.size(); ++index)
    {
        const juce::String text = keyChordText(keys[index]);
        const bool is_first_of_group = std::ranges::find(seen_texts, text) == seen_texts.end();
        juce::Component* const chip =
            findDescendant(editor, "keymap_chip_" + id_hex + "_" + juce::String{index});
        if (is_first_of_group)
        {
            seen_texts.push_back(text);
            REQUIRE(chip != nullptr);
            CHECK(dynamic_cast<juce::Button*>(chip)->getButtonText() == text);
        }
        else
        {
            CHECK(chip == nullptr);
        }
    }

    // A group replacement swaps every chord the chip represents for the captured one.
    std::vector<int> first_group_indices;
    for (int index = 0; index < keys.size(); ++index)
    {
        if (keyChordText(keys[index]) == keyChordText(keys[0]))
        {
            first_group_indices.push_back(index);
        }
    }
    const juce::KeyPress f7{juce::KeyPress::F7Key};
    editor.applyBindingChange(EditorCommandId::GridFiner, f7, first_group_indices);
    CHECK(mappings.findCommandForKeyPress(f7) == grid_finer);
    for (const int index : first_group_indices)
    {
        CHECK(mappings.findCommandForKeyPress(keys[index]) != grid_finer);
    }
}

// Rows rebuild on the mapping set's change broadcast, so external rebinds refresh the chips.
TEST_CASE("KeymapEditorView refreshes rows on mapping changes", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    const juce::KeyPress f9{juce::KeyPress::F9Key};
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f9, {0});
    mappings.sendSynchronousChangeMessage();

    const juce::String id_hex =
        juce::String::toHexString(toJuceCommandId(EditorCommandId::ToggleUndoHistory));
    const auto& first_chip =
        findRequiredDescendant<juce::Button>(editor, "keymap_chip_" + id_hex + "_0");
    CHECK(first_chip.getButtonText() == keyChordText(f9));
}

// The actions window hosts the custom editor over the editor's one mapping set and survives
// closes: the close button hides it, and reopening keeps the same content alive.
TEST_CASE("ActionsWindow hosts the editor and survives closes", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    ActionsWindow window{view.commandManager(), nullptr};
    const auto& editor_view =
        findRequiredDescendant<KeymapEditorView>(window, "actions_editor_view");
    CHECK(editor_view.isVisible());
    CHECK_FALSE(window.isVisible());

    window.open();
    CHECK(window.isVisible());

    window.closeButtonPressed();
    CHECK_FALSE(window.isVisible());

    window.open();
    CHECK(window.isVisible());
}

// The Edit menu exposes the dialog as a command item, and performing the command creates and
// shows the top-level window.
TEST_CASE("EditorView opens the actions window from the command", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setState(core::EditorViewState{});

    const int command_id = toJuceCommandId(EditorCommandId::ShowActions);
    CHECK(requiredMenuItem(view.getMenuForIndex(1, "Edit"), command_id).isEnabled);

    view.commandManager().invokeDirectly(command_id, false);

    juce::TopLevelWindow* actions_window = nullptr;
    for (int index = 0; index < juce::TopLevelWindow::getNumTopLevelWindows(); ++index)
    {
        juce::TopLevelWindow* const window = juce::TopLevelWindow::getTopLevelWindow(index);
        if (window->getName() == "Actions")
        {
            actions_window = window;
        }
    }
    REQUIRE(actions_window != nullptr);
    CHECK(actions_window->isVisible());
}

} // namespace rock_hero::editor::ui
