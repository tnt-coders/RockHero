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

} // namespace

juce::String keyChordText(const juce::KeyPress& key, ShiftedCharacterResolver resolve_shifted)
{
    // Mirrors getTextDescription's printable-character key-code range; everything outside it
    // (named keys, numpad codes) keeps its explicit JUCE description.
    constexpr int printable_first = 33;
    constexpr int printable_last_exclusive = 176;

    const int key_code = key.getKeyCode();
    const juce::ModifierKeys modifiers = key.getModifiers();
    if (modifiers.isShiftDown() && key_code >= printable_first &&
        key_code < printable_last_exclusive)
    {
        const juce::juce_wchar base = static_cast<juce::juce_wchar>(key_code);
        const juce::juce_wchar resolved = resolve_shifted(base);

        // Collapse only when Shift changes the character by more than case: "shift + /"
        // becomes "?", but letters stay explicit — a bare capital reads ambiguously against
        // the lowercase chip convention.
        if (resolved != 0 && juce::CharacterFunctions::toLowerCase(resolved) !=
                                 juce::CharacterFunctions::toLowerCase(base))
        {
            juce::String text;
            if (modifiers.isCtrlDown())
            {
                text << "ctrl + ";
            }
            if (modifiers.isAltDown())
            {
                text << "alt + ";
            }
            return text + juce::String::charToString(resolved);
        }
    }
    return key.getTextDescription();
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

    juce::String shortcut_text;
    for (const juce::KeyPress& key :
         command_manager.getKeyMappings()->getKeyPressesAssignedToCommand(command_id))
    {
        if (shortcut_text.isNotEmpty())
        {
            shortcut_text << ", ";
        }
        shortcut_text << keyChordText(key);
    }
    item.shortcutKeyDescription = shortcut_text;

    menu.addItem(std::move(item));
}

} // namespace rock_hero::editor::ui
