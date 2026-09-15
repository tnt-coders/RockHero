/*!
\file keymap_ownership.h
\brief The one-owner law for chords in the command mapping set.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Gives a chord to one command, taking it from whichever command held it.

The one-owner law: a chord names one command, so JUCE's dispatch, which invokes the first ENABLED
owner of a chord, and its lookup, which returns the first owner, can never disagree. Every path that
writes a binding goes through this — the keymap editor's assign and its reset to defaults, and the
restore of a saved keymap, whose chord a newer default may since have claimed for another command
(the user's override wins, and that command loses the chord). JUCE's own `addKeyPress` is never
trusted to do this: the conflict removal its documentation describes does not exist in code.

\param mappings The command manager's mapping set.
\param command Command that takes the chord.
\param key The chord.
\param insert_index Position within the command's key list, or -1 to append.
*/
void assignKeyPressToCommand(
    juce::KeyPressMappingSet& mappings, juce::CommandID command, const juce::KeyPress& key,
    int insert_index);

/*!
\brief Removes one chord from one command, leaving other commands' bindings alone.

The inverse write, for restoring a saved keymap's removals: a chord the user took off a command.
Removing a chord the command does not hold — a default that no longer exists — does nothing.

\param mappings The command manager's mapping set.
\param command Command that loses the chord.
\param key The chord.
*/
void removeKeyPressFromCommand(
    juce::KeyPressMappingSet& mappings, juce::CommandID command, const juce::KeyPress& key);

} // namespace rock_hero::editor::ui
