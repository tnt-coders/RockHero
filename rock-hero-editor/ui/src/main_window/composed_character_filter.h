/*!
\file composed_character_filter.h
\brief Drops OS-composed characters before they reach the keybind mapping set.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Decides whether a press is a character the OS composed rather than a key the user struck.

Split out of the listener so the rule is testable without the platform's key-state table.

\param key Press exactly as delivered to the key listener.
\param key_currently_down Whether the press's own key code is held down right now, as
`juce::KeyPress::isKeyCurrentlyDown` reports it.
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

Install this on a top-level window AFTER the key mapping set: JUCE walks a component's key
listeners in reverse registration order (`juce_ComponentPeer.cpp:206-214`), so the listener
registered last is the first to see a press.
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
