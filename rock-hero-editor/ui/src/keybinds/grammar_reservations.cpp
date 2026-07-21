#include "keybinds/grammar_reservations.h"

namespace rock_hero::editor::ui
{

const std::vector<GrammarReservation>& grammarReservations()
{
    // Wording mirrors docs/plans/in-progress/keymap-matrix.md; the interaction model owns the
    // semantics. Each family is split fine enough that every chip is a plain key description
    // in the same lowercase style keyChordText gives the command rows' chips.
    static const std::vector<GrammarReservation> reservations{
        {"Move caret", {"arrow keys"}},
        {"Jump by measure", {"ctrl + left/right"}},
        {"Jump to chart start / end", {"home", "end"}},
        {"Jump by section", {"page up", "page down"}},
        {"Extend time selection", {"shift + left/right"}},
        {"Extend by measure", {"ctrl + shift + left/right"}},
        {"Extend by section", {"shift + page up/down"}},
        {"Extend to chart start / end", {"shift + home", "shift + end"}},
        {"Move selection", {"alt + arrows"}},
        {"Move selection finely", {"ctrl + alt + arrows"}},
        {"Resize sustain", {"shift + alt + left/right"}},
        {"Resize sustain finely", {"ctrl + shift + alt + left/right"}},
        {"Shift frets", {"shift + alt + up/down"}},
        {"Type fret / value", {"0-9"}},
        {"Delete selection", {"delete", "backspace"}},
        {"Insert at caret", {"insert"}},
        {"Cancel / dismiss", {"escape"}},
        {"Grid finer / coarser", {"+", "-"}},
        {"Zoom in / out", {"ctrl + +", "ctrl + -"}},
    };
    return reservations;
}

bool isReservedGrammarChord(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey ||
        code == juce::KeyPress::upKey || code == juce::KeyPress::downKey ||
        code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey ||
        code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey ||
        code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey ||
        code == juce::KeyPress::insertKey || code == juce::KeyPress::escapeKey ||
        code == juce::KeyPress::returnKey)
    {
        return true;
    }

    if ((code >= '0' && code <= '9') ||
        (code >= juce::KeyPress::numberPad0 && code <= juce::KeyPress::numberPad9))
    {
        return true;
    }

    // The +/- grid/zoom family, in every key shape the grammar decoder's union match accepts.
    return code == '=' || code == '+' || code == '-' || code == juce::KeyPress::numberPadAdd ||
           code == juce::KeyPress::numberPadSubtract;
}

} // namespace rock_hero::editor::ui
