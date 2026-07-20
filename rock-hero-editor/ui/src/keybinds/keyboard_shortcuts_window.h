/*!
\file keyboard_shortcuts_window.h
\brief Themed window hosting the stock JUCE key-mapping editor over the editor's keymap.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace rock_hero::editor::ui
{

/*!
\brief Non-modal tool window presenting the rebindable keymap (Edit > Keyboard Shortcuts...).

Hosts the stock `juce::KeyMappingEditorComponent` over the editor's one key mapping set (plan 46
Phase 3 decision, 2026-07-20: adopt stock and judge it in action — the registry/persistence
substrate is dialog-agnostic, so a later custom rebuild stays cheap). The window stays alive
across closes — the close button only hides it — so reopening keeps tree state, and rebinds
apply live through the mapping set's own change broadcasts (key dispatch, menu shortcut text,
and the keymap persistence all listen to the same set).

Two seams adapt stock to the editor:

- A window-local look-and-feel routes the component's confirm popups through
  `juce::LookAndFeel_V2::createAlertWindow`, skipping `juce::LookAndFeel_V4`'s hard-coded 50 px
  bounds padding and 40 px button shift — the defect the themed message boxes avoid by
  constructing dialogs directly (see `shared/themed_message_box.h`). The popups then match the
  editor's actual dialog look, which is the default-painted `juce::AlertWindow` style. The
  popups resolve this look-and-feel because the alert helper reads it from its associated
  component, whose parent chain ends at this window.
- Non-rebindable commands render as fixed rows through
  `juce::ApplicationCommandInfo::readOnlyInKeyEditor`, which the view's `getCommandInfo`
  derives from the registry's rebindable flag.
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

    /*! \brief Detaches the window-local look-and-feel before it is destroyed. */
    ~KeyboardShortcutsWindow() override;

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

    /*! \brief Hides the window; its content and tree state stay alive for reopening. */
    void closeButtonPressed() override;

private:
    // Routes the mapping editor's confirm popups around LookAndFeel_V4's padded alert factory.
    class KeymapDialogLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        /*!
        \brief Builds a confirm popup without LookAndFeel_V4's padding defect.

        Delegates to `juce::LookAndFeel_V2::createAlertWindow`, which carries the exact
        button/modal-result semantics the stock mapping editor's callbacks depend on, and skips
        V4's 50 px bounds padding and 40 px button shift on top of it.

        \param title Popup title text.
        \param message Popup body text.
        \param button1 First button label.
        \param button2 Second button label; empty when unused.
        \param button3 Third button label; empty when unused.
        \param icon_type Icon shown beside the message.
        \param num_buttons Number of buttons to create.
        \param associated_component Component the popup is positioned around.
        \return The popup window; ownership passes to the caller.
        */
        juce::AlertWindow* createAlertWindow(
            const juce::String& title, const juce::String& message, const juce::String& button1,
            const juce::String& button2, const juce::String& button3,
            juce::MessageBoxIconType icon_type, int num_buttons,
            juce::Component* associated_component) override;
    };

    // Declared before the mapping editor so the look-and-feel outlives its consumers.
    KeymapDialogLookAndFeel m_look_and_feel;

    // The stock key-mapping editor, listening to the mapping set for live refresh.
    juce::KeyMappingEditorComponent m_mapping_editor;

    // Owner used to center the window on open; guarded because the owner may close first.
    juce::Component::SafePointer<juce::Component> m_centering_component;
};

} // namespace rock_hero::editor::ui
