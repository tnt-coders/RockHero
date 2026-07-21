#include "keybinds/plugin_window_shortcut_sync.h"

#include "keybinds/editor_command_id.h"

#include <rock_hero/common/audio/plugin/plugin_window_shortcuts.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Converts one command's key presses into the seam's layout-neutral chords.
[[nodiscard]] std::vector<common::audio::PluginWindowShortcutChord> chordsForCommand(
    juce::KeyPressMappingSet& mappings, EditorCommandId command)
{
    std::vector<common::audio::PluginWindowShortcutChord> chords;
    for (const juce::KeyPress& key :
         mappings.getKeyPressesAssignedToCommand(toJuceCommandId(command)))
    {
        chords.push_back(common::audio::pluginWindowChordFromKeyPress(key));
    }
    return chords;
}

} // namespace

PluginWindowShortcutSync::PluginWindowShortcutSync(
    juce::ApplicationCommandManager& command_manager, common::audio::IPluginHost& plugin_host)
    : m_command_manager(command_manager)
    , m_plugin_host(plugin_host)
{
    push();
    m_command_manager.getKeyMappings()->addChangeListener(this);
}

PluginWindowShortcutSync::~PluginWindowShortcutSync()
{
    m_command_manager.getKeyMappings()->removeChangeListener(this);
}

void PluginWindowShortcutSync::push()
{
    juce::KeyPressMappingSet& mappings = *m_command_manager.getKeyMappings();
    common::audio::PluginWindowShortcutBindings bindings;
    bindings.undo = chordsForCommand(mappings, EditorCommandId::Undo);
    bindings.redo = chordsForCommand(mappings, EditorCommandId::Redo);
    bindings.play_pause = chordsForCommand(mappings, EditorCommandId::PlayPause);
    m_plugin_host.setPluginWindowShortcuts(std::move(bindings));
}

void PluginWindowShortcutSync::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    push();
}

} // namespace rock_hero::editor::ui
