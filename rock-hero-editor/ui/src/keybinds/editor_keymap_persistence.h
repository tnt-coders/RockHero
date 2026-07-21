/*!
\file editor_keymap_persistence.h
\brief Persists the command mapping set's user overrides through the editor settings port.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>

namespace rock_hero::editor::ui
{

/*!
\brief Restores and saves the keymap's diff-versus-defaults XML.

Construction restores any stored overrides into the command manager's key mapping set —
every command must already be registered, which the owning composition guarantees by
constructing this after the view. Stored entries whose command id is unknown (a newer
editor's blob) are dropped before restore, so a downgraded settings file cannot trip the
mapping set's debug assertion. After restore, every mapping-set change saves the current
diff; a defaults-only keymap clears the stored value entirely (pure diff persistence, so
shipped default changes merge under user overrides).
*/
class EditorKeymapPersistence final : private juce::ChangeListener
{
public:
    /*!
    \brief Restores stored overrides and begins saving mapping changes.
    \param command_manager Command manager whose key mapping set is persisted.
    \param settings Settings port holding the opaque keymap blob.
    */
    EditorKeymapPersistence(
        juce::ApplicationCommandManager& command_manager, core::IEditorSettings& settings);

    /*! \brief Stops listening to mapping-set changes. */
    ~EditorKeymapPersistence() override;

    /*! \brief Copying is disabled because the listener registration is identity-based. */
    EditorKeymapPersistence(const EditorKeymapPersistence&) = delete;

    /*! \brief Copy assignment is disabled because the listener registration is identity-based. */
    EditorKeymapPersistence& operator=(const EditorKeymapPersistence&) = delete;

    /*! \brief Moving is disabled because the listener registration is identity-based. */
    EditorKeymapPersistence(EditorKeymapPersistence&&) = delete;

    /*! \brief Move assignment is disabled because the listener registration is identity-based. */
    EditorKeymapPersistence& operator=(EditorKeymapPersistence&&) = delete;

private:
    // Saves the mapping set's current diff, equality-gated against the stored blob so the
    // restore's own change broadcast and repeated notifications write nothing.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::ApplicationCommandManager& m_command_manager;
    core::IEditorSettings& m_settings;
};

} // namespace rock_hero::editor::ui
