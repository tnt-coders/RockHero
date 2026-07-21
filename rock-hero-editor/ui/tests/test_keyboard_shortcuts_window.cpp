#include "keybinds/editor_command_registry.h"
#include "keybinds/grammar_reservations.h"
#include "keybinds/keyboard_shortcuts_window.h"
#include "keybinds/keymap_editor_view.h"

#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// The custom editor lists every registry command as a row: rebindable rows carry enabled chips
// plus the add affordance, non-rebindable rows render inert chips and no add affordance.
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
        juce::Component* const add_chip = findDescendant(editor, "keymap_add_" + id_hex);
        CHECK((add_chip != nullptr) == spec.rebindable);
        if (!spec.default_keypresses.empty())
        {
            auto& first_chip =
                findRequiredDescendant<juce::Button>(editor, "keymap_chip_" + id_hex + "_0");
            CHECK(first_chip.isEnabled() == spec.rebindable);
            CHECK(
                first_chip.getButtonText() == spec.default_keypresses.front().getTextDescription());
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

// Non-rebindable commands refuse binding changes even through the direct apply path, so the
// fixed core trio can never gain an alias the plugin-window hook would not mirror.
TEST_CASE("KeymapEditorView refuses changes to non-rebindable commands", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    const juce::KeyPress f10{juce::KeyPress::F10Key};
    editor.applyBindingChange(EditorCommandId::Undo, f10, -1);
    CHECK(mappings.findCommandForKeyPress(f10) == 0);
    CHECK(
        mappings.findCommandForKeyPress(
            juce::KeyPress{'z', juce::ModifierKeys::commandModifier, 0}) ==
        toJuceCommandId(EditorCommandId::Undo));
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

// Reserved chords are refused at the apply layer, and chords owned by a non-rebindable command
// can never be stolen through the overwrite-and-clear dance.
TEST_CASE("KeymapEditorView refuses reserved and fixed-owner chords", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    KeymapEditorView editor{view.commandManager()};
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();

    // Binding a command to a grammar key is refused.
    const juce::KeyPress right_arrow{juce::KeyPress::rightKey};
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, right_arrow, -1);
    CHECK(mappings.findCommandForKeyPress(right_arrow) == 0);

    // Stealing Ctrl+Z from Undo through the conflict dance is refused.
    const juce::KeyPress ctrl_z{'z', juce::ModifierKeys::commandModifier, 0};
    editor.applyBindingChange(EditorCommandId::TogglePreview3D, ctrl_z, -1);
    CHECK(mappings.findCommandForKeyPress(ctrl_z) == toJuceCommandId(EditorCommandId::Undo));
    CHECK_FALSE(
        mappings.getKeyPressesAssignedToCommand(toJuceCommandId(EditorCommandId::TogglePreview3D))
            .contains(ctrl_z));
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
    CHECK(first_chip.getButtonText() == f9.getTextDescription());
}

// The shortcuts window hosts the custom editor over the editor's one mapping set and survives
// closes: the close button hides it, and reopening keeps the same content alive.
TEST_CASE("KeyboardShortcutsWindow hosts the editor and survives closes", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    KeyboardShortcutsWindow window{view.commandManager(), nullptr};
    auto& editor_view =
        findRequiredDescendant<KeymapEditorView>(window, "keyboard_shortcuts_editor_view");
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
TEST_CASE("EditorView opens the keyboard shortcuts window from the command", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setState(core::EditorViewState{});

    const int command_id = toJuceCommandId(EditorCommandId::ShowKeyboardShortcuts);
    CHECK(requiredMenuItem(view.getMenuForIndex(1, "Edit"), command_id).isEnabled);

    view.commandManager().invokeDirectly(command_id, false);

    juce::TopLevelWindow* shortcuts_window = nullptr;
    for (int index = 0; index < juce::TopLevelWindow::getNumTopLevelWindows(); ++index)
    {
        juce::TopLevelWindow* const window = juce::TopLevelWindow::getTopLevelWindow(index);
        if (window->getName() == "Keyboard Shortcuts")
        {
            shortcuts_window = window;
        }
    }
    REQUIRE(shortcuts_window != nullptr);
    CHECK(shortcuts_window->isVisible());
}

} // namespace rock_hero::editor::ui
