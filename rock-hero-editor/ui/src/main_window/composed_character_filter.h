/*!
\file composed_character_filter.h
\brief Drops OS-composed characters before they reach the keybind mapping set.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace rock_hero::editor::ui
{

/*!
\brief Names the numpad key that can produce the same character as the given character key code.

A press carries the character the OS produced, not the key that produced it, and the numpad's
operator and digit keys produce characters the main rows also produce. Asking the key-state table
about the character alone therefore asks about the main-row key only, so the numpad twin has to be
asked separately.

\param key_code Key code exactly as the press carries it.
\return The numpad key code producing that character, or nothing when no numpad key produces it.
*/
[[nodiscard]] std::optional<int> numpadTwinOf(int key_code) noexcept;

/*!
\brief Decides whether a press is a character the OS composed rather than a key the user struck.

Split out of the listener so the rule is testable without the platform's key-state table.

\param key Press exactly as delivered to the key listener.
\param key_currently_down Whether any key that produces this press's character is held down right
now — its own key code or, per \ref numpadTwinOf, its numpad twin.
\return True when the press must be swallowed: no modifier, a plain character code, nothing held.
*/
[[nodiscard]] bool isComposedCharacterPress(
    const juce::KeyPress& key, bool key_currently_down) noexcept;

/*!
\brief Swallows key presses the OS synthesized from a modifier sequence instead of a struck key.

Windows composes `Alt` + numpad digits into one character: while `Alt` is held each numpad digit
accumulates a decimal code, and releasing `Alt` makes `TranslateMessage` — which JUCE calls for
every message its own pump dispatches (`juce_Messaging_windows.cpp:150`) — post a single `WM_CHAR`
carrying that code, so `Alt` plus `7`, `6` arrives as `L`. The app cannot prevent the composition:
it happens inside `TranslateMessage`, and not calling that would also kill dead keys and ordinary
text input.

What arrives is indistinguishable from a real press at the mapping level. `doKeyChar`
(`juce_Windowing_windows.cpp:3169-3211`) passes the character to `handleKeyPress` unfiltered,
control codes included: the synthesized message's scan code resolves to no character, so the
unmodified-character remap at `:3198-3205` leaves the key code as the composed value, and
`updateKeyModifiers` at `:3171` already reads `Alt` as released. A struck `L` and a composed `L`
therefore match the same chord — and codes 8, 9, 13, 27 and 32 are `backspaceKey`, `tabKey`,
`returnKey`, `escapeKey` and `spaceKey` verbatim on Windows (`:703-709`), so a composed control
code reaches those bindings the same way.

The one datum that separates the two is that nothing is held down for a composed character. Each
platform records a real press in its key-state table BEFORE dispatching that press — Windows reads
the hardware asynchronously (`GetAsyncKeyState`, `juce_Windowing_windows.cpp:1758`, reached from
`:5608`), macOS inserts into `keysCurrentlyDown` in `redirectKeyDown` ahead of `handleKeyEvent`
(`juce_NSViewComponentPeer_mac.mm:926-933`), and X11 sets its bit in `handleKeyPressEvent` ahead of
`handleKeyPress` (`juce_XWindowSystem_linux.cpp:3447`, `:3552`) — so `isKeyCurrentlyDown` is true
inside `keyPressed` for anything the user actually struck. The rule therefore needs no platform
guard; it is simply inert where the OS synthesizes no characters.

Asking that question needs \ref numpadTwinOf, because the press names a CHARACTER and one character
can come from two physical keys. Numpad `+` reaches `doKeyChar` as `'+'` — `doKeyDown` has no
`VK_ADD` case (`juce_Windowing_windows.cpp:3077-3163`) and the numpad remap in `doKeyChar` covers
digits only (`:3176-3195`) — and `isKeyCurrentlyDown('+')` converts that character through
`VkKeyScan`, which answers with the MAIN-ROW `VK_OEM_PLUS` (`:5608-5617`). The key actually held is
`VK_ADD`, so the bare question reported "not down" and the filter swallowed every numpad `+`, `-`,
`*`, `/` and `.`. Asking the twin as well fixes it without weakening the rule: the composed
character comes from NEITHER key, so both questions answer false for it.

Install this on a top-level window; the registration order carries no rule. JUCE offers a
component's key listeners the press BEFORE that component's own `keyPressed`
(`juce_ComponentPeer.cpp:200-217`), so the filter always sees a press ahead of the command dispatch
the window performs in `keyPressed`.
*/
class ComposedCharacterFilter final : public juce::KeyListener
{
public:
    /*!
    \brief Consumes the press when it is an OS-composed character.
    \param key Press offered to the listener.
    \param originating_component Component the press was dispatched for; the rule ignores it.
    \return True when the press was swallowed, which stops every later listener and binding.
    */
    bool keyPressed(const juce::KeyPress& key, juce::Component* originating_component) override;
};

} // namespace rock_hero::editor::ui
