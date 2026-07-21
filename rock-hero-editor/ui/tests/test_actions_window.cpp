#include "keybinds/actions_window.h"
#include "keybinds/editor_command_registry.h"
#include "keybinds/grammar_reservations.h"
#include "keybinds/key_chord_text.h"
#include "keybinds/keymap_editor_view.h"

#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// The custom editor lists every registry command as a rebindable row: enabled chips per
// binding plus the add affordance, with the fixed grammar reference gathered at the bottom.
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
            auto& first_chip =
                findRequiredDescendant<juce::Button>(editor, "keymap_chip_" + id_hex + "_0");
            CHECK(first_chip.isEnabled());
            // Compared through the shared formatter: chip text collapses shifted characters
            // ("?"), so raw getTextDescription would mismatch on layout-resolvable chords.
            CHECK(first_chip.getButtonText() == keyChordText(spec.default_keypresses.front()));
        }
    }

    // The fixed grammar reference sits below every command row.
    juce::Component* const last_command_row = findDescendant(
        editor,
        "keymap_row_" +
            juce::String::toHexString(toJuceCommandId(EditorCommandId::InsertToneChange)));
    juce::Component* const grammar_row = findDescendant(editor, "keymap_grammar_row_0");
    REQUIRE(last_command_row != nullptr);
    REQUIRE(grammar_row != nullptr);
    CHECK(grammar_row->getY() > last_command_row->getY());
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
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    const juce::KeyPress f9{juce::KeyPress::F9Key};

    // Adding a new binding.
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f9, -1);
    CHECK(
        mappings.findCommandForKeyPress(f9) == toJuceCommandId(EditorCommandId::ToggleUndoHistory));

    // Assigning the same chord elsewhere strips it from the previous owner.
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, f9, -1);
    CHECK(mappings.findCommandForKeyPress(f9) == toJuceCommandId(EditorCommandId::TogglePreview3D));
    CHECK_FALSE(
        mappings.getKeyPressesAssignedToCommand(toJuceCommandId(EditorCommandId::ToggleUndoHistory))
            .contains(f9));

    // Replacing binding 0 (F8) swaps it for the new chord.
    const juce::KeyPress f7{juce::KeyPress::F7Key};
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f7, 0);
    CHECK(
        mappings.findCommandForKeyPress(f7) == toJuceCommandId(EditorCommandId::ToggleUndoHistory));
    CHECK(mappings.findCommandForKeyPress(juce::KeyPress{juce::KeyPress::F8Key}) == 0);

    // Removing a binding empties its slot.
    editor.removeBinding(EditorCommandId::ToggleUndoHistory, 0);
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
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    const juce::KeyPress ctrl_z{'z', juce::ModifierKeys::commandModifier, 0};

    // Undo gains an F10 alias.
    const juce::KeyPress f10{juce::KeyPress::F10Key};
    editor.applyBindingChange(EditorCommandId::Undo, f10, -1);
    CHECK(mappings.findCommandForKeyPress(f10) == toJuceCommandId(EditorCommandId::Undo));

    // Another command may take Ctrl+Z through the one-owner dance.
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, ctrl_z, -1);
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
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    const juce::KeyPress f8{juce::KeyPress::F8Key};
    const juce::KeyPress f9{juce::KeyPress::F9Key};

    // Rebind Undo History off its F8 default, then let another command take F8.
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f9, 0);
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, f8, -1);
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

// Grammar keys are reserved by physical key across every modifier shape: the decoder runs
// before the mapping set, so a command bound to a grammar chord would only sometimes fire.
TEST_CASE("Grammar reservations cover the decoder's keys", "[ui][keybinds]")
{
    constexpr int command = juce::ModifierKeys::commandModifier;
    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;

    CHECK(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::rightKey}));
    CHECK(isReservedGrammarChord(
        juce::KeyPress{juce::KeyPress::leftKey, juce::ModifierKeys{command | shift | alt}, 0}));
    CHECK(isReservedGrammarChord(juce::KeyPress{'5', juce::ModifierKeys{}, 0}));
    CHECK(isReservedGrammarChord(
        juce::KeyPress{juce::KeyPress::numberPad7, juce::ModifierKeys{}, 0}));
    CHECK(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::deleteKey}));
    CHECK(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::insertKey}));
    CHECK(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::escapeKey}));
    CHECK(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::returnKey}));
    CHECK(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::homeKey}));
    CHECK(isReservedGrammarChord(
        juce::KeyPress{juce::KeyPress::pageDownKey, juce::ModifierKeys{shift}, 0}));
    CHECK(isReservedGrammarChord(juce::KeyPress{'=', juce::ModifierKeys{command}, 0}));
    CHECK(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::numberPadSubtract}));

    CHECK_FALSE(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::F9Key}));
    CHECK_FALSE(isReservedGrammarChord(juce::KeyPress{'o', juce::ModifierKeys{command}, 0}));
    CHECK_FALSE(isReservedGrammarChord(juce::KeyPress{juce::KeyPress::spaceKey}));
}

// Grammar-reserved chords are refused at the apply layer: the decoder runs before the mapping
// set, so a command bound to one would only sometimes fire.
TEST_CASE("KeymapEditorView refuses reserved chords", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    const juce::KeyPress right_arrow{juce::KeyPress::rightKey};
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, right_arrow, -1);
    CHECK(mappings.findCommandForKeyPress(right_arrow) == 0);
}

// The dialog lists the fixed editing/navigation reference rows below the command rows.
TEST_CASE("KeymapEditorView lists the fixed grammar reference", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};

    for (int index = 0; index < static_cast<int>(grammarReservations().size()); ++index)
    {
        CHECK(findDescendant(editor, "keymap_grammar_row_" + juce::String{index}) != nullptr);
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
    editor.applyBindingChange(EditorCommandId::ToggleUndoHistory, f9, 0);
    mappings.sendSynchronousChangeMessage();

    const juce::String id_hex =
        juce::String::toHexString(toJuceCommandId(EditorCommandId::ToggleUndoHistory));
    auto& first_chip = findRequiredDescendant<juce::Button>(editor, "keymap_chip_" + id_hex + "_0");
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
    auto& editor_view = findRequiredDescendant<KeymapEditorView>(window, "actions_editor_view");
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
