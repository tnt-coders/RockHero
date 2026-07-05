/*!
\file plugin_move_index.h
\brief Pure index translation for moving an existing plugin within a Tracktion plugin list.
*/

#pragma once

namespace rock_hero::common::audio
{

/*!
\brief Translates a raw Tracktion insertion index for moving an existing plugin.

Tracktion captures the target sibling's ValueTree index before removing the plugin from its old
parent, so source plugins before that sibling need the requested raw index shifted left by one.

\param original_tracktion_index Raw list index the plugin currently occupies.
\param destination_tracktion_index Raw list index requested for the plugin.
\return Insertion index to pass to Tracktion for the move.
*/
[[nodiscard]] constexpr int tracktionInsertionIndexForExistingPluginMove(
    int original_tracktion_index, int destination_tracktion_index) noexcept
{
    if (destination_tracktion_index < 0)
    {
        return destination_tracktion_index;
    }

    if (original_tracktion_index >= 0 && original_tracktion_index < destination_tracktion_index)
    {
        return destination_tracktion_index - 1;
    }

    return destination_tracktion_index;
}

} // namespace rock_hero::common::audio
