#include "plugin/plugin_window_shortcuts.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::common::audio
{

namespace
{

// Lowercases the ASCII letters so 'Z' and 'z' identify one chord; other characters pass through.
[[nodiscard]] char32_t lowercasedCharacter(char32_t character) noexcept
{
    if (character >= U'A' && character <= U'Z')
    {
        return character - U'A' + U'a';
    }
    return character;
}

// Maps the JUCE named-key codes both decode paths must agree on; other codes return None.
// If-chains rather than a switch because JUCE key codes are runtime constants.
[[nodiscard]] PluginWindowShortcutKey namedKeyForJuceKeyCode(int key_code) noexcept
{
    if (key_code >= juce::KeyPress::F1Key && key_code <= juce::KeyPress::F12Key)
    {
        return static_cast<PluginWindowShortcutKey>(
            static_cast<int>(PluginWindowShortcutKey::F1) + (key_code - juce::KeyPress::F1Key));
    }
    if (key_code == juce::KeyPress::leftKey)
    {
        return PluginWindowShortcutKey::ArrowLeft;
    }
    if (key_code == juce::KeyPress::rightKey)
    {
        return PluginWindowShortcutKey::ArrowRight;
    }
    if (key_code == juce::KeyPress::upKey)
    {
        return PluginWindowShortcutKey::ArrowUp;
    }
    if (key_code == juce::KeyPress::downKey)
    {
        return PluginWindowShortcutKey::ArrowDown;
    }
    if (key_code == juce::KeyPress::homeKey)
    {
        return PluginWindowShortcutKey::Home;
    }
    if (key_code == juce::KeyPress::endKey)
    {
        return PluginWindowShortcutKey::End;
    }
    if (key_code == juce::KeyPress::pageUpKey)
    {
        return PluginWindowShortcutKey::PageUp;
    }
    if (key_code == juce::KeyPress::pageDownKey)
    {
        return PluginWindowShortcutKey::PageDown;
    }
    if (key_code == juce::KeyPress::deleteKey)
    {
        return PluginWindowShortcutKey::Delete;
    }
    if (key_code == juce::KeyPress::insertKey)
    {
        return PluginWindowShortcutKey::Insert;
    }
    if (key_code == juce::KeyPress::backspaceKey)
    {
        return PluginWindowShortcutKey::Backspace;
    }
    if (key_code == juce::KeyPress::tabKey)
    {
        return PluginWindowShortcutKey::Tab;
    }
    if (key_code == juce::KeyPress::returnKey)
    {
        return PluginWindowShortcutKey::Return;
    }
    if (key_code == juce::KeyPress::escapeKey)
    {
        return PluginWindowShortcutKey::Escape;
    }
    return PluginWindowShortcutKey::None;
}

// Maps the JUCE numpad key codes onto the characters their main-row twins produce, so a numpad
// binding and a main-row binding identify the same chord. If-chains rather than a switch
// because JUCE key codes are runtime constants.
[[nodiscard]] char32_t characterForNumpadKeyCode(int key_code) noexcept
{
    if (key_code >= juce::KeyPress::numberPad0 && key_code <= juce::KeyPress::numberPad9)
    {
        return static_cast<char32_t>(U'0' + (key_code - juce::KeyPress::numberPad0));
    }
    if (key_code == juce::KeyPress::numberPadAdd)
    {
        return U'+';
    }
    if (key_code == juce::KeyPress::numberPadSubtract)
    {
        return U'-';
    }
    if (key_code == juce::KeyPress::numberPadMultiply)
    {
        return U'*';
    }
    if (key_code == juce::KeyPress::numberPadDivide)
    {
        return U'/';
    }
    if (key_code == juce::KeyPress::numberPadDecimalPoint)
    {
        return U'.';
    }
    return 0;
}

[[nodiscard]] bool chordListMatches(
    const std::vector<PluginWindowShortcutChord>& chords, const PluginWindowShortcutChord& chord)
{
    for (const PluginWindowShortcutChord& candidate : chords)
    {
        if (candidate == chord)
        {
            return true;
        }
    }
    return false;
}

} // namespace

PluginWindowShortcutBindings defaultPluginWindowShortcutBindings()
{
    // The historical hardcoded trio plus the Ctrl+Shift+Z redo alias the editor's registry
    // ships as a default, so an editor-less engine behaves like the editor's default keymap.
    PluginWindowShortcutBindings bindings;
    bindings.undo.push_back(PluginWindowShortcutChord{.character = U'z', .ctrl = true});
    bindings.redo.push_back(PluginWindowShortcutChord{.character = U'y', .ctrl = true});
    bindings.redo.push_back(
        PluginWindowShortcutChord{.character = U'z', .ctrl = true, .shift = true});
    bindings.play_pause.push_back(PluginWindowShortcutChord{.character = U' '});
    return bindings;
}

std::optional<PluginWindowCommand> matchPluginWindowCommand(
    const PluginWindowShortcutBindings& bindings, const PluginWindowShortcutChord& chord)
{
    if (chord.character == 0 && chord.named_key == PluginWindowShortcutKey::None)
    {
        return std::nullopt;
    }

    if (chordListMatches(bindings.undo, chord))
    {
        return PluginWindowCommand::Undo;
    }
    if (chordListMatches(bindings.redo, chord))
    {
        return PluginWindowCommand::Redo;
    }
    if (chordListMatches(bindings.play_pause, chord))
    {
        return PluginWindowCommand::PlayPause;
    }
    return std::nullopt;
}

PluginWindowShortcutChord pluginWindowChordFromKeyPress(const juce::KeyPress& key)
{
    PluginWindowShortcutChord chord;
    const juce::ModifierKeys modifiers = key.getModifiers();
    chord.ctrl = modifiers.isCommandDown();
    chord.alt = modifiers.isAltDown();
    chord.shift = modifiers.isShiftDown();

    const int key_code = key.getKeyCode();
    chord.named_key = namedKeyForJuceKeyCode(key_code);
    if (chord.named_key != PluginWindowShortcutKey::None)
    {
        return chord;
    }

    if (const char32_t numpad_character = characterForNumpadKeyCode(key_code);
        numpad_character != 0)
    {
        chord.character = numpad_character;
        return chord;
    }

    if (key_code == juce::KeyPress::spaceKey)
    {
        chord.character = U' ';
        return chord;
    }

    // Main-row keys carry their unshifted character as the key code (the reliable identity —
    // the text character is not trustworthy while Ctrl is held); anything non-printable that
    // is not a recognized named key stays unidentified and can never match.
    if (key_code > ' ' && key_code < 0x7F)
    {
        chord.character = lowercasedCharacter(static_cast<char32_t>(key_code));
    }
    return chord;
}

} // namespace rock_hero::common::audio
