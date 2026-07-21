/*!
\file keyboard_shortcuts_window.h
\brief Themed window hosting the editor's custom key-binding editor.
*/

#pragma once

#include "keybinds/keymap_editor_view.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Non-modal tool window presenting the rebindable keymap (Edit > Keyboard Shortcuts...).

Hosts the custom `KeymapEditorView` over the editor's one key mapping set (plan 46 Phase 3: the
stock component shipped first and its recorded custom-rebuild trigger fired — the themed stock
dialog read as off-product in live use). The window stays alive across closes — the close
button only hides it — and rebinds apply live through the mapping set's own change broadcasts
(key dispatch, menu shortcut text, and the keymap persistence all listen to the same set).
*/
class KeyboardShortcutsWindow final : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the window over the editor's command manager.
    \param command_manager Command manager owning the key mapping set; must outlive this window.
    \param centering_component Component whose top-level window centers this one; may be null.
    */
    KeyboardShortcutsWindow(
        juce::ApplicationCommandManager& command_manager, juce::Component* centering_component);

    /*! \brief Destroys the window and its owned editor view. */
    ~KeyboardShortcutsWindow() override = default;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    KeyboardShortcutsWindow(const KeyboardShortcutsWindow&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    KeyboardShortcutsWindow& operator=(const KeyboardShortcutsWindow&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    KeyboardShortcutsWindow(KeyboardShortcutsWindow&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    KeyboardShortcutsWindow& operator=(KeyboardShortcutsWindow&&) = delete;

    /*! \brief Shows the window, centring it over its owner when opening from hidden. */
    void open();

    /*! \brief Hides the window; its content stays alive for reopening. */
    void closeButtonPressed() override;

private:
    // The custom key-binding editor, listening to the mapping set for live refresh.
    KeymapEditorView m_editor_view;

    // Owner used to center the window on open; guarded because the owner may close first.
    juce::Component::SafePointer<juce::Component> m_centering_component;
};

} // namespace rock_hero::editor::ui
