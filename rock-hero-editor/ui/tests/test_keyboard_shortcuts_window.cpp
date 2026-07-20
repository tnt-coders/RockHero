#include "keybinds/editor_command_registry.h"
#include "keybinds/keyboard_shortcuts_window.h"

#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// The shortcuts window hosts the stock mapping editor over the editor's one mapping set and
// survives closes: the close button hides it, and reopening keeps the same content alive.
TEST_CASE("KeyboardShortcutsWindow hosts the mapping editor and survives closes", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    KeyboardShortcutsWindow window{view.commandManager(), nullptr};
    auto& mapping_editor = findRequiredDescendant<juce::KeyMappingEditorComponent>(
        window, "keyboard_shortcuts_mapping_editor");
    CHECK(&mapping_editor.getMappings() == view.commandManager().getKeyMappings());
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
