/*!
\file plugin_window_shortcut_sync.h
\brief Pushes the editor's Undo/Redo/Play-Pause chords into hosted plugin windows.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>

namespace rock_hero::editor::ui
{

/*!
\brief Mirrors the keymap's Undo/Redo/Play-Pause bindings into the plugin-window shortcut seam.

Hosted plugin editor windows claim these three commands before the plugin can swallow them, and
with every command user-rebindable the actual chords live in the key mapping set. This unit
converts them through the shared layout-neutral chord model and pushes them via
`IPluginHost::setPluginWindowShortcuts` — once at construction (after the keymap restore, which
the owning composition guarantees by constructing this after the keymap persistence) and again
on every mapping-set change, so a rebind takes effect in open plugin windows immediately.
*/
class PluginWindowShortcutSync final : private juce::ChangeListener
{
public:
    /*!
    \brief Pushes the current bindings and begins following mapping changes.
    \param command_manager Command manager owning the key mapping set.
    \param plugin_host Plugin host receiving the mirrored chords; must outlive this object.
    */
    PluginWindowShortcutSync(
        juce::ApplicationCommandManager& command_manager, common::audio::IPluginHost& plugin_host);

    /*! \brief Stops listening to mapping-set changes. */
    ~PluginWindowShortcutSync() override;

    /*! \brief Copying is disabled because the listener registration is identity-based. */
    PluginWindowShortcutSync(const PluginWindowShortcutSync&) = delete;

    /*! \brief Copy assignment is disabled because the listener registration is identity-based. */
    PluginWindowShortcutSync& operator=(const PluginWindowShortcutSync&) = delete;

    /*! \brief Moving is disabled because the listener registration is identity-based. */
    PluginWindowShortcutSync(PluginWindowShortcutSync&&) = delete;

    /*! \brief Move assignment is disabled because the listener registration is identity-based. */
    PluginWindowShortcutSync& operator=(PluginWindowShortcutSync&&) = delete;

private:
    // Extracts the three commands' chords from the mapping set and pushes them to the host.
    void push();

    // Re-pushes on every mapping-set change (rebinds, resets, keymap restore).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::ApplicationCommandManager& m_command_manager;
    common::audio::IPluginHost& m_plugin_host;
};

} // namespace rock_hero::editor::ui
