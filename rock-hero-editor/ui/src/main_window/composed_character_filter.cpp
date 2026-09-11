#include "main_window/composed_character_filter.h"

namespace rock_hero::editor::ui
{

// Only the keys that produce a character a main-row key also produces need a twin. The numpad's
// remaining keys (Enter, and the editing keys the digits become with NumLock off) carry
// `extendedKeyModifier` already, so they never reach the held-down test at all.
std::optional<int> numpadTwinOf(const int key_code) noexcept
{
    if (key_code >= '0' && key_code <= '9')
    {
        return juce::KeyPress::numberPad0 + (key_code - '0');
    }

    switch (key_code)
    {
        case '+':
            return juce::KeyPress::numberPadAdd;
        case '-':
            return juce::KeyPress::numberPadSubtract;
        case '*':
            return juce::KeyPress::numberPadMultiply;
        case '/':
            return juce::KeyPress::numberPadDivide;
        case '.':
            return juce::KeyPress::numberPadDecimalPoint;
        default:
            return std::nullopt;
    }
}

// Three facts, all of which a composed character has and a struck key cannot have together.
//
// A modifier rules the press out because the composition is delivered after the modifier that
// built it was released, so it always arrives bare. An extended key code rules it out because
// composition yields a character: every arrow, function, numpad and editing key carries
// `extendedKeyModifier` (0x10000) and so sits far above the character range, which also keeps the
// numpad's own digits — the keys the user physically pressed — out of the rule.
//
// The held-down test is the one that does the work, and it is deliberately the LAST word rather
// than a heuristic: the key code the OS invented for a composed character belongs to a key nobody
// touched. Its one false positive is a real press whose character message is processed after the
// user already let the key go, which needs the message pump to stall for a whole tap.
bool isComposedCharacterPress(const juce::KeyPress& key, const bool key_currently_down) noexcept
{
    if (key.getModifiers().isAnyModifierKeyDown())
    {
        return false;
    }

    const int key_code = key.getKeyCode();
    if (key_code <= 0 || key_code > 0xFF)
    {
        return false;
    }

    return !key_currently_down;
}

// Runs ahead of the key mapping set, which is why the window registers this listener last (see the
// class documentation). The key code needs no case folding before the key-state query: every
// platform's `isKeyCurrentlyDown` already accepts either case — Windows converts through
// `VkKeyScan` (`juce_Windowing_windows.cpp:5608-5620`), macOS tries both cases
// (`juce_NSViewComponentPeer_mac.mm:2963-2981`), and X11 converts the keysym to its hardware
// keycode (`juce_XWindowSystem_linux.cpp:2488-2515`), which is shared by the two cases. It does
// need the numpad twin, which those same conversions cannot reach — see the class documentation.
bool ComposedCharacterFilter::keyPressed(
    const juce::KeyPress& key, juce::Component* /*originating_component*/)
{
    const int key_code = key.getKeyCode();
    const std::optional<int> twin = numpadTwinOf(key_code);
    const bool key_currently_down =
        juce::KeyPress::isKeyCurrentlyDown(key_code) ||
        (twin.has_value() && juce::KeyPress::isKeyCurrentlyDown(twin.value()));

    return isComposedCharacterPress(key, key_currently_down);
}

} // namespace rock_hero::editor::ui
