#include "keybinds/keymap_ownership.h"

namespace rock_hero::editor::ui
{

// Strip first, then add: removeKeyPress(key) takes the chord from every command holding it, so
// the add that follows makes exactly one owner whatever the mapping set held before.
void assignKeyPressToCommand(
    juce::KeyPressMappingSet& mappings, const juce::CommandID command, const juce::KeyPress& key,
    const int insert_index)
{
    mappings.removeKeyPress(key);
    mappings.addKeyPress(command, key, insert_index);
}

// The mapping set removes by index within one command's list, so the chord is located there
// first; a chord the command does not hold is left as the no-op it should be.
void removeKeyPressFromCommand(
    juce::KeyPressMappingSet& mappings, const juce::CommandID command, const juce::KeyPress& key)
{
    const int index = mappings.getKeyPressesAssignedToCommand(command).indexOf(key);
    if (index >= 0)
    {
        mappings.removeKeyPress(command, index);
    }
}

} // namespace rock_hero::editor::ui
