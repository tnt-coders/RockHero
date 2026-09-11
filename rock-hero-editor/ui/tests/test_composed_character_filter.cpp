#include "main_window/composed_character_filter.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a press the way a peer does: modifier flags, no text character (the rule ignores it).
[[nodiscard]] juce::KeyPress press(int key_code, int modifier_flags = 0)
{
    return juce::KeyPress{key_code, juce::ModifierKeys{modifier_flags}, 0};
}

} // namespace

// Verifies the composed-character rule: a bare character press with its own key NOT held is the
// only shape the OS can have synthesized, and it is the only shape that gets swallowed.
TEST_CASE("Composed-character rule swallows only unheld bare character presses", "[ui][keybinds]")
{
    SECTION("a bare letter nobody is holding is the composed shape")
    {
        CHECK(isComposedCharacterPress(press('l'), false));
        CHECK(isComposedCharacterPress(press('L'), false));
    }

    SECTION("the same letter passes through while its key is held")
    {
        CHECK_FALSE(isComposedCharacterPress(press('l'), true));
        CHECK_FALSE(isComposedCharacterPress(press('L'), true));
    }

    SECTION("control codes the composition can produce are covered too")
    {
        // 8, 9, 13, 27 and 32 are backspace, tab, return, escape and space verbatim on Windows,
        // so Alt+0+0+2+7 would otherwise reach the Escape binding.
        CHECK(isComposedCharacterPress(press(juce::KeyPress::backspaceKey), false));
        CHECK(isComposedCharacterPress(press(juce::KeyPress::tabKey), false));
        CHECK(isComposedCharacterPress(press(juce::KeyPress::returnKey), false));
        CHECK(isComposedCharacterPress(press(juce::KeyPress::escapeKey), false));
        CHECK(isComposedCharacterPress(press(juce::KeyPress::spaceKey), false));
    }

    SECTION("a modified press is never the composed shape, held or not")
    {
        // The composition delivers its character only once the modifier that built it is gone, so
        // a press that still carries one cannot be it — and an Alt chord must keep working.
        CHECK_FALSE(isComposedCharacterPress(press('5', juce::ModifierKeys::altModifier), false));
        CHECK_FALSE(isComposedCharacterPress(press('5', juce::ModifierKeys::altModifier), true));
        CHECK_FALSE(
            isComposedCharacterPress(press('s', juce::ModifierKeys::commandModifier), false));
        CHECK_FALSE(isComposedCharacterPress(press('l', juce::ModifierKeys::shiftModifier), false));
    }

    SECTION("extended keys pass through even when the key-state table says not held")
    {
        // Composition yields a character, and every non-character key sits above the character
        // range behind extendedKeyModifier — the numpad's own digits included.
        CHECK_FALSE(isComposedCharacterPress(press(juce::KeyPress::insertKey), false));
        CHECK_FALSE(isComposedCharacterPress(press(juce::KeyPress::deleteKey), false));
        CHECK_FALSE(isComposedCharacterPress(press(juce::KeyPress::leftKey), false));
        CHECK_FALSE(isComposedCharacterPress(press(juce::KeyPress::F5Key), false));
        CHECK_FALSE(isComposedCharacterPress(press(juce::KeyPress::numberPad7), false));
    }

    SECTION("an empty press is not a character")
    {
        CHECK_FALSE(isComposedCharacterPress(juce::KeyPress{}, false));
    }
}

} // namespace rock_hero::editor::ui
