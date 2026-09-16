/*!
\file editor_command_registry.h
\brief Static registry describing every editor command's display data and default chords.
*/

#pragma once

#include "keybinds/editor_command_id.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Display data and default key chords for one registered editor command.

The registry is the single authoritative table behind the command manager: `getCommandInfo`
derives names, categories, and default keypresses from it, and the locked-table unit test pins
its ids and chords so an accidental renumbering or default change fails the build. Every
command is user-rebindable with no exceptions — the grammar verbs are registered commands too
(plan 53 Phase 1b, total rebindability), so the interaction grammar's modifier algebra lives on
only as the shape of the defaults below.
*/
struct EditorCommandSpec final
{
    /*! \brief Stable command identity (the persistence key, locked forever). */
    EditorCommandId id;

    /*! \brief Base display name shown in menus and the shortcuts dialog. */
    const char* name{""};

    /*! \brief Category grouping for the shortcuts dialog. */
    const char* category{""};

    /*! \brief Default key chords installed into the mapping set; empty for menu-only commands. */
    std::vector<juce::KeyPress> default_keypresses{};
};

/*!
\brief Returns the full editor command registry table.
\return One spec per registered command, in stable registry order.
*/
[[nodiscard]] const std::vector<EditorCommandSpec>& editorCommandRegistry();

/*!
\brief Looks up a registry entry by its JUCE command id.
\param command_id JUCE command id to find.
\return Matching spec, or nullptr when the id is not a registered editor command.
*/
[[nodiscard]] const EditorCommandSpec* findEditorCommandSpec(juce::CommandID command_id);

/*!
\brief Whether a command acts where the keyboard focus stands, so the view keeps that focus in
sight once the command has run.

Read off the shortcuts-dialog category, which already groups the commands by what they act on:
the navigation, selection, authoring and marker verbs act at the focus; file, edit-history, view,
transport, grid and menu commands do not, and a saved file or a toggled panel must never scroll
the window back to a caret the user scrolled away from. Two rows answer against their category
and are named here: the tone-change chord authors at the cursor although it sits with the tone
file verbs, and Cancel dismisses although it sits with the selection verbs.

\param spec Registry entry to classify.
\return True when the view should reveal the focus after performing the command.
*/
[[nodiscard]] bool editorCommandRevealsFocus(const EditorCommandSpec& spec);

} // namespace rock_hero::editor::ui
