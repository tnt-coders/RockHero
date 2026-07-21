#include "keybinds/grammar_reservations.h"

namespace rock_hero::editor::ui
{

const std::vector<GrammarReservation>& grammarReservations()
{
    // Wording mirrors docs/plans/in-progress/keymap-matrix.md; the interaction model owns the
    // semantics. Rows describe composed families, not single chords, which is why these render
    // as text rather than binding chips.
    static const std::vector<GrammarReservation> reservations{
        {"Move caret", "Arrows  (Ctrl = measure jump)"},
        {"Jump", "Home / End,  PageUp / PageDn = section"},
        {"Extend time selection", "Shift+Arrows  (+Ctrl = measure),  Shift+PageUp/Dn / Home / End"},
        {"Move selection", "Alt+Arrows  (+Ctrl = fine)"},
        {"Resize sustain", "Shift+Alt+Left/Right  (+Ctrl = fine)"},
        {"Shift frets", "Shift+Alt+Up/Down"},
        {"Type fret / value", "0-9  (numpad too)"},
        {"Delete selection", "Del / Backspace"},
        {"Insert at caret", "Ins"},
        {"Cancel / dismiss", "Esc  (gesture, then caret, then selection)"},
        {"Grid finer / coarser", "+ / -"},
        {"Zoom in / out", "Ctrl++ / Ctrl+-"},
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
