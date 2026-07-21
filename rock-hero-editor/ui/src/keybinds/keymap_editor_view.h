/*!
\file keymap_editor_view.h
\brief Custom themed editor for the command registry's key bindings.
*/

#pragma once

#include "keybinds/editor_command_id.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief The keyboard-shortcuts editor: registry commands, binding chips, capture, and reset.

Custom-built against the public `juce::KeyPressMappingSet` API (plan 46 Phase 3's recorded
custom-rebuild trigger fired 2026-07-20: the themed stock component read as off-product in live
use). The view lists every registry command under its category, one row per command with its
binding chips right-aligned; clicking a chip offers change/remove, the trailing `+` chip
captures a new binding through a press-a-key dialog, and conflicts resolve through the
overwrite-and-clear flow — a themed confirm naming the current owner, then remove-then-add, so
exactly one owner keeps a chord (`addKeyPress` alone must never be trusted to resolve
conflicts; its documented conflict removal does not exist in code). Every command is
rebindable — the grammar verbs included (plan 53 Phase 1b, total rebindability).

Rows rebuild from the mapping set on every change broadcast, which also keeps the view live
against rebinds arriving from anywhere else; the broadcasts are asynchronous, so a rebuild
never destroys the control whose click started it.
*/
class KeymapEditorView final : public juce::Component, private juce::ChangeListener
{
public:
    /*!
    \brief Creates the editor over the command manager's key mapping set.
    \param command_manager Command manager owning the mapping set; must outlive this view.
    */
    explicit KeymapEditorView(juce::ApplicationCommandManager& command_manager);

    /*! \brief Stops listening to mapping-set changes. */
    ~KeymapEditorView() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    KeymapEditorView(const KeymapEditorView&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    KeymapEditorView& operator=(const KeymapEditorView&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    KeymapEditorView(KeymapEditorView&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    KeymapEditorView& operator=(KeymapEditorView&&) = delete;

    /*! \brief Fills the view background with the panel color. \param g Graphics context. */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the scrolling row list above the reset strip. */
    void resized() override;

    /*!
    \brief Applies a captured chord to a command with overwrite-and-clear semantics.

    Removes the chord from whichever command currently owns it, removes the replaced binding
    when one is being changed, then adds the chord — the remove-then-add dance, public so the
    mapping-set semantics stay directly testable. Callers gate any confirmation beforehand;
    this method applies unconditionally.

    \param command Command receiving the chord.
    \param key Captured chord to assign.
    \param replace_index Zero-based binding index being changed, or -1 to add a new binding.
    */
    void applyBindingChange(EditorCommandId command, const juce::KeyPress& key, int replace_index);

    /*!
    \brief Removes one of a command's bindings.
    \param command Command losing a binding.
    \param key_index Zero-based index of the binding to remove.
    */
    void removeBinding(EditorCommandId command, int key_index);

    /*!
    \brief Restores one command's bindings to the registry defaults.

    Offered per row (right-click, and in each chip's menu) — the affordance whose absence from
    the stock component's private rows helped decide the custom build. One-owner semantics hold
    through the reset: each default chord is first stripped from whatever command took it in
    the meantime, because the mapping set's own per-command reset performs no conflict cleanup
    of its own.

    \param command Command to restore to its registry defaults.
    */
    void resetCommandToDefault(EditorCommandId command);

private:
    class ChipButton;
    class CategoryHeader;
    class CommandRow;

    // Rebuilds the header and row components from the registry and the current mappings.
    void rebuildRows();

    // Opens the press-a-key capture dialog for a new or replaced binding.
    void beginCapture(EditorCommandId command, int replace_index);

    // Applies a captured chord, first confirming an overwrite when another command owns it.
    void confirmAndApply(EditorCommandId command, const juce::KeyPress& key, int replace_index);

    // Confirms and performs the reset of every mapping to the registry defaults.
    void confirmResetAll();

    // True when a command's current bindings equal its registry defaults (positional compare;
    // a reset normalizes ordering anyway, so a false negative only enables a harmless reset).
    [[nodiscard]] bool isCommandAtDefaults(EditorCommandId command) const;

    // Rebuilds rows when the mapping set changes (rebinds here or anywhere else).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::ApplicationCommandManager& m_command_manager;

    // Scrolling list of category headers and command rows.
    juce::Viewport m_viewport;
    juce::Component m_row_list;
    std::vector<std::unique_ptr<juce::Component>> m_rows;

    // Bottom-strip control resetting every mapping to the registry defaults, behind a confirm.
    juce::TextButton m_reset_button{"Reset All to Defaults"};
};

} // namespace rock_hero::editor::ui
