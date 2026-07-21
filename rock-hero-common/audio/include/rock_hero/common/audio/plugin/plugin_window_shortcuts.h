/*!
\file plugin_window_shortcuts.h
\brief Injected, rebindable plugin-window shortcut bindings and their layout-neutral matcher.

A hosted plugin editor window claims a small set of editor shortcuts before the plugin can
swallow them (undo, redo, play/pause). The editor owns the actual chords now that all commands
are user-rebindable, so it injects them here as layout-neutral values; the concrete window and
its Win32 message hook match incoming keys against them. Keeping the value types and the pure
matcher free of JUCE, Tracktion, and Win32 lets the matching logic be unit-tested headlessly and
keeps the public port lean.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace juce
{
class KeyPress;
} // namespace juce

namespace rock_hero::common::audio
{

/*! \brief Commands a hosted plugin editor window forwards to the owning application workflow. */
enum class PluginWindowCommand : std::uint8_t
{
    /*! \brief Forward the editor's Undo shortcut from a plugin window. */
    Undo,

    /*! \brief Forward the editor's Redo shortcut from a plugin window. */
    Redo,

    /*! \brief Forward the editor's Play/Pause shortcut from a plugin window. */
    PlayPause,
};

/*!
\brief A non-character key a shortcut chord can name.

Character keys (letters, digits, punctuation, Space) are matched by their base character through
the active keyboard layout, so they need no entry here; only keys that produce no character do.
What matters is that both decode paths (JUCE key presses and Win32 key messages) agree on these
values, not completeness of the keyboard.
*/
enum class PluginWindowShortcutKey : std::uint8_t
{
    /*! \brief The chord identifies a character key, not a named key. */
    None,

    /*! \brief Function key F1. */
    F1,
    /*! \brief Function key F2. */
    F2,
    /*! \brief Function key F3. */
    F3,
    /*! \brief Function key F4. */
    F4,
    /*! \brief Function key F5. */
    F5,
    /*! \brief Function key F6. */
    F6,
    /*! \brief Function key F7. */
    F7,
    /*! \brief Function key F8. */
    F8,
    /*! \brief Function key F9. */
    F9,
    /*! \brief Function key F10. */
    F10,
    /*! \brief Function key F11. */
    F11,
    /*! \brief Function key F12. */
    F12,
    /*! \brief Left arrow key. */
    ArrowLeft,
    /*! \brief Right arrow key. */
    ArrowRight,
    /*! \brief Up arrow key. */
    ArrowUp,
    /*! \brief Down arrow key. */
    ArrowDown,
    /*! \brief Home key. */
    Home,
    /*! \brief End key. */
    End,
    /*! \brief Page Up key. */
    PageUp,
    /*! \brief Page Down key. */
    PageDown,
    /*! \brief Delete key. */
    Delete,
    /*! \brief Insert key. */
    Insert,
    /*! \brief Backspace key. */
    Backspace,
    /*! \brief Tab key. */
    Tab,
    /*! \brief Return / Enter key. */
    Return,
    /*! \brief Escape key. */
    Escape,
};

/*!
\brief One shortcut chord, identified layout-neutrally.

A chord names its key either as a base `character` (lowercased for letters; the character the
key produces on the active keyboard layout, so an `Alt+;` binding matches wherever `;`
physically sits) or as a `named_key` for keys that produce no character. Exactly one identifies
the key: `named_key` is `None` for character chords, and `character` is 0 for named-key chords.
*/
struct PluginWindowShortcutChord
{
    /*! \brief Base character (lowercased), or 0 when the chord names a non-character key. */
    char32_t character{0};

    /*! \brief Named non-character key, or `None` when the chord uses \ref character. */
    PluginWindowShortcutKey named_key{PluginWindowShortcutKey::None};

    /*! \brief Whether Ctrl (the command modifier) participates. */
    bool ctrl{false};

    /*! \brief Whether Alt participates. */
    bool alt{false};

    /*! \brief Whether Shift participates. */
    bool shift{false};

    /*!
    \brief Compares two chords for exact key and modifier equality.
    \param lhs Left-hand chord.
    \param rhs Right-hand chord.
    \return True when both name the same key with the same modifiers.
    */
    friend bool operator==(
        const PluginWindowShortcutChord& lhs, const PluginWindowShortcutChord& rhs) = default;
};

/*!
\brief The chord lists a plugin window forwards for each command.

Each command may hold several chords (a command can have alternative bindings). Empty lists are
valid; an empty binding set means the command is not forwarded from plugin windows at all.
*/
struct PluginWindowShortcutBindings
{
    /*! \brief Chords forwarded as Undo. */
    std::vector<PluginWindowShortcutChord> undo;

    /*! \brief Chords forwarded as Redo. */
    std::vector<PluginWindowShortcutChord> redo;

    /*! \brief Chords forwarded as Play/Pause. */
    std::vector<PluginWindowShortcutChord> play_pause;
};

/*!
\brief The built-in bindings applied when the editor has injected none.

Keeps the concrete engine behavior-identical to the historical hardcoded trio (Ctrl+Z, Ctrl+Y,
Space) when it runs without an editor pushing bindings.

\return The default Undo/Redo/Play-Pause chords.
*/
[[nodiscard]] PluginWindowShortcutBindings defaultPluginWindowShortcutBindings();

/*!
\brief Matches a decoded key against injected bindings.
\param bindings Injected chord lists.
\param chord Layout-neutral chord decoded from a key event.
\return The matched command, or absence when no binding matches.
*/
[[nodiscard]] std::optional<PluginWindowCommand> matchPluginWindowCommand(
    const PluginWindowShortcutBindings& bindings, const PluginWindowShortcutChord& chord);

/*!
\brief Builds a layout-neutral chord from a JUCE key press.

Used by the JUCE `keyPressed` path and by the editor when it converts registry bindings for
injection, so both sides agree on how a key press maps to a chord. Named keys are recognized by
key code first (so Return/Tab/arrows/function keys become named chords), otherwise the key is a
character chord keyed by its lowercased text character.

\param key JUCE key press.
\return The equivalent chord.
*/
[[nodiscard]] PluginWindowShortcutChord pluginWindowChordFromKeyPress(const juce::KeyPress& key);

} // namespace rock_hero::common::audio
