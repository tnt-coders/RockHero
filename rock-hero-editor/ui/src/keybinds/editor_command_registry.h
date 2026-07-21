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
command is user-rebindable (the fixed-trio decision was reversed 2026-07-20 once the
plugin-window mirror generalized to arbitrary chords); only the interaction grammar's reserved
chords are off-limits, enforced by `grammar_reservations`.
*/
struct EditorCommandSpec final
{
    /*! \brief Stable command identity (the persistence key, locked forever). */
    EditorCommandId id{};

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

} // namespace rock_hero::editor::ui
