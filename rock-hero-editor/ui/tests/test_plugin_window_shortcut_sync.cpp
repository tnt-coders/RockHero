#include "keybinds/editor_command_id.h"
#include "keybinds/plugin_window_shortcut_sync.h"

#include <rock_hero/common/audio/testing/recording_plugin_host.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// Construction pushes the current trio chords; every mapping change re-pushes, so a rebind
// takes effect in open plugin windows immediately.
TEST_CASE("PluginWindowShortcutSync mirrors the trio bindings", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    common::audio::testing::RecordingPluginHost plugin_host;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    const PluginWindowShortcutSync sync{view.commandManager(), plugin_host};
    CHECK(plugin_host.window_shortcuts_set_count == 1);

    // The first push carries the registry defaults through the layout-neutral chord model.
    const common::audio::PluginWindowShortcutBindings& defaults = plugin_host.last_window_shortcuts;
    REQUIRE(defaults.undo.size() == 1);
    CHECK(
        defaults.undo.front() ==
        common::audio::PluginWindowShortcutChord{.character = U'z', .ctrl = true});
    REQUIRE(defaults.redo.size() == 2);
    CHECK(
        defaults.redo.back() ==
        common::audio::PluginWindowShortcutChord{.character = U'z', .ctrl = true, .shift = true});
    REQUIRE(defaults.play_pause.size() == 1);
    CHECK(
        defaults.play_pause.front() == common::audio::PluginWindowShortcutChord{.character = U' '});

    // A rebind re-pushes: Play/Pause gains an Alt+; alias that mirrors into plugin windows.
    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    mappings.addKeyPress(
        toJuceCommandId(EditorCommandId::PlayPause),
        juce::KeyPress{';', juce::ModifierKeys{juce::ModifierKeys::altModifier}, 0});
    mappings.sendSynchronousChangeMessage();
    CHECK(plugin_host.window_shortcuts_set_count >= 2);
    bool found_alt_semicolon = false;
    for (const common::audio::PluginWindowShortcutChord& chord :
         plugin_host.last_window_shortcuts.play_pause)
    {
        if (chord == common::audio::PluginWindowShortcutChord{.character = U';', .alt = true})
        {
            found_alt_semicolon = true;
        }
    }
    CHECK(found_alt_semicolon);
}

} // namespace rock_hero::editor::ui
