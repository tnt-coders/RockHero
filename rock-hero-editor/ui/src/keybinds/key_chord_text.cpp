#include "keybinds/key_chord_text.h"

#include <array>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rock_hero::editor::ui
{

namespace
{

// Asks the active keyboard layout what a key types with Shift held. Platform-specific because
// no generic form exists: neither JUCE nor the C++ standard library exposes a keyboard-layout
// query, and the alternative — a hardcoded US punctuation table — would render the wrong
// character on any other layout. On unsupported platforms the formatter simply never
// collapses, falling back to the explicit "shift + /" form.
[[nodiscard]] juce::juce_wchar liveLayoutShiftedCharacter(
    [[maybe_unused]] juce::juce_wchar base_character)
{
#if JUCE_WINDOWS
    // VkKeyScanW yields the virtual key whose plain press types the base character; a nonzero
    // modifier byte means the base itself needs modifiers on this layout, so there is no plain
    // key to add Shift to.
    const SHORT key_and_modifiers = VkKeyScanW(static_cast<WCHAR>(base_character));
    if (key_and_modifiers == -1 || HIBYTE(key_and_modifiers) != 0)
    {
        return 0;
    }

    const UINT virtual_key = LOBYTE(key_and_modifiers);
    const UINT scan_code = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    std::array<BYTE, 256> key_state{};
    key_state[VK_SHIFT] = 0x80;
    std::array<WCHAR, 4> translated{};

    // Flag bit 2 (0x4) keeps ToUnicodeEx from mutating the thread's dead-key state (documented
    // for Windows 10 1607+) — without it this query could corrupt in-flight dead-key
    // composition elsewhere in the app.
    const int character_count = ToUnicodeEx(
        virtual_key,
        scan_code,
        key_state.data(),
        translated.data(),
        static_cast<int>(translated.size()),
        0x4,
        GetKeyboardLayout(0));

    // Anything but exactly one character (dead key = negative, none/multiple) is unresolvable.
    if (character_count != 1)
    {
        return 0;
    }
    return translated[0];
#else
    return 0;
#endif
}

// Modifier prefix — capitalized names, joined by the house middle dot ("Ctrl·Shift·"). Shift
// is omitted by the collapse path, which spends it on the resolved character.
[[nodiscard]] juce::String modifierPrefix(const juce::ModifierKeys& modifiers, bool with_shift)
{
    juce::String text;
    if (modifiers.isCtrlDown())
    {
        text << "Ctrl" << keyChordJoiner();
    }
    if (with_shift && modifiers.isShiftDown())
    {
        text << "Shift" << keyChordJoiner();
    }
    if (modifiers.isAltDown())
    {
        text << "Alt" << keyChordJoiner();
    }
    return text;
}

// Canonical Windows-convention names for the non-character keys, replacing JUCE's lowercase
// idiosyncrasies ("spacebar", "return", "cursor left"). Arrows are bare direction words —
// the glyph alternatives all fail at chip size (thin arrows are barely legible and fell to
// font substitution in the running editor; heavy arrows risk color-emoji presentation).
// "Num" abbreviates JUCE's "numpad" per convention (REAPER/Windows "Num"). Returns empty when
// the key is not a named key.
[[nodiscard]] juce::String namedKeyText(int key_code)
{
    struct NamedKey
    {
        int code;
        const char* name;
    };
    // Runtime table (not a switch) because JUCE key-code constants are runtime values.
    const NamedKey named_keys[] = {
        {juce::KeyPress::leftKey, "Left"},
        {juce::KeyPress::rightKey, "Right"},
        {juce::KeyPress::upKey, "Up"},
        {juce::KeyPress::downKey, "Down"},
        {juce::KeyPress::spaceKey, "Space"},
        {juce::KeyPress::returnKey, "Enter"},
        {juce::KeyPress::escapeKey, "Esc"},
        {juce::KeyPress::backspaceKey, "Backspace"},
        {juce::KeyPress::deleteKey, "Delete"},
        {juce::KeyPress::insertKey, "Insert"},
        {juce::KeyPress::tabKey, "Tab"},
        {juce::KeyPress::homeKey, "Home"},
        {juce::KeyPress::endKey, "End"},
        {juce::KeyPress::pageUpKey, "Page Up"},
        {juce::KeyPress::pageDownKey, "Page Down"},
        {juce::KeyPress::numberPadAdd, "Num +"},
        {juce::KeyPress::numberPadSubtract, "Num -"},
        {juce::KeyPress::numberPadMultiply, "Num *"},
        {juce::KeyPress::numberPadDivide, "Num /"},
        {juce::KeyPress::numberPadDecimalPoint, "Num ."},
        {juce::KeyPress::numberPadEquals, "Num ="},
        {juce::KeyPress::numberPadSeparator, "Num ,"},
        {juce::KeyPress::numberPadDelete, "Num Delete"},
    };
    for (const NamedKey& named : named_keys)
    {
        if (key_code == named.code)
        {
            return named.name;
        }
    }
    if (key_code >= juce::KeyPress::numberPad0 && key_code <= juce::KeyPress::numberPad9)
    {
        return "Num " + juce::String{key_code - juce::KeyPress::numberPad0};
    }
    if (key_code >= juce::KeyPress::F1Key && key_code <= juce::KeyPress::F16Key)
    {
        return "F" + juce::String{1 + key_code - juce::KeyPress::F1Key};
    }
    if (key_code >= juce::KeyPress::F17Key && key_code <= juce::KeyPress::F24Key)
    {
        return "F" + juce::String{17 + key_code - juce::KeyPress::F17Key};
    }
    if (key_code >= juce::KeyPress::F25Key && key_code <= juce::KeyPress::F35Key)
    {
        return "F" + juce::String{25 + key_code - juce::KeyPress::F25Key};
    }
    return {};
}

} // namespace

juce::String keyChordJoiner()
{
    // A spaced U+00B7 MIDDLE DOT (" · ") — Latin-1, present in every font (the math-block dot
    // operator U+22C5 is not, and font substitution already killed the arrow glyphs). Built by
    // codepoint because plain narrow literals assert on non-ASCII in juce::String; spacing per
    // in-app review 2026-07-21.
    return " " + juce::String::charToString(juce::juce_wchar{0x00B7}) + " ";
}

juce::String keyChordText(const juce::KeyPress& key, ShiftedCharacterResolver resolve_shifted)
{
    // Mirrors getTextDescription's printable-character key-code range; everything outside it
    // is a named key or falls through to JUCE's description.
    constexpr int printable_first = 33;
    constexpr int printable_last_exclusive = 176;

    const int key_code = key.getKeyCode();
    const juce::ModifierKeys modifiers = key.getModifiers();

    if (const juce::String named = namedKeyText(key_code); named.isNotEmpty())
    {
        return modifierPrefix(modifiers, /*with_shift=*/true) + named;
    }

    if (key_code >= printable_first && key_code < printable_last_exclusive)
    {
        const juce::juce_wchar base = static_cast<juce::juce_wchar>(key_code);
        if (modifiers.isShiftDown())
        {
            // Collapse only when Shift changes the character by more than case: "Shift+/"
            // becomes "?". Letters stay explicit ("Shift+Z"), because the convention displays
            // plain letter chords uppercase too — a bare "Z" could not be told apart from the
            // unshifted binding.
            const juce::juce_wchar resolved = resolve_shifted(base);
            if (resolved != 0 && juce::CharacterFunctions::toLowerCase(resolved) !=
                                     juce::CharacterFunctions::toLowerCase(base))
            {
                return modifierPrefix(modifiers, /*with_shift=*/false) +
                       juce::String::charToString(resolved);
            }
        }
        return modifierPrefix(modifiers, /*with_shift=*/true) +
               juce::String::charToString(juce::CharacterFunctions::toUpperCase(base));
    }

    // Exotic keys (media keys and the like): JUCE's description with its lowercase modifier
    // prefixes rewritten into the convention.
    return key.getTextDescription()
        .replace("ctrl + ", "Ctrl" + keyChordJoiner())
        .replace("shift + ", "Shift" + keyChordJoiner())
        .replace("alt + ", "Alt" + keyChordJoiner());
}

juce::String keyChordText(const juce::KeyPress& key)
{
    return keyChordText(key, &liveLayoutShiftedCharacter);
}

void addEditorCommandItem(
    juce::PopupMenu& menu, juce::ApplicationCommandManager& command_manager,
    EditorCommandId command)
{
    const juce::CommandID command_id = toJuceCommandId(command);
    const juce::ApplicationCommandInfo* const registered =
        command_manager.getCommandForID(command_id);
    jassert(registered != nullptr);
    if (registered == nullptr)
    {
        return;
    }

    // Mirrors PopupMenu::addCommandItem's construction, with one change: the shortcut text is
    // pre-filled through keyChordText, because the popup derives its own raw
    // getTextDescription text only when this field arrives empty.
    juce::ApplicationCommandInfo info{*registered};
    juce::ApplicationCommandTarget* const target =
        command_manager.getTargetForCommand(command_id, info);

    juce::PopupMenu::Item item;
    item.text = info.shortName;
    item.itemID = static_cast<int>(command_id);
    item.commandManager = &command_manager;
    item.isEnabled =
        target != nullptr && (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
    item.isTicked = (info.flags & juce::ApplicationCommandInfo::isTicked) != 0;

    // Display-equal chords (OS key-shape twins like Shift+'=' and the numpad-arrival '+')
    // render once — they are one logical key to the user.
    juce::StringArray chord_texts;
    for (const juce::KeyPress& key :
         command_manager.getKeyMappings()->getKeyPressesAssignedToCommand(command_id))
    {
        chord_texts.addIfNotAlreadyThere(keyChordText(key));
    }
    item.shortcutKeyDescription = chord_texts.joinIntoString(", ");

    menu.addItem(std::move(item));
}

} // namespace rock_hero::editor::ui
