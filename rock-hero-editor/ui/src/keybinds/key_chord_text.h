/*!
\file key_chord_text.h
\brief The one display formatter for key chords, shared by every surface that names a binding.
*/

#pragma once

#include "keybinds/editor_command_id.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Resolves the character a key produces with Shift held on the active keyboard layout.

`base_character` is the key's unmodified character (which is what JUCE stores as the key code
for printable keys). Returns 0 when the shifted character is unknown — not a plain key on this
layout, a dead key, or no layout query available on this platform.
*/
using ShiftedCharacterResolver = juce::juce_wchar (*)(juce::juce_wchar base_character);

/*!
\brief Returns the "·" (middle dot) joining chord parts in every display ("Ctrl·Z").

The small dot is the editor's house separator (user decision 2026-07-21): unlike the
conventional "+", it can never collide with the `+`/`-` keys ("Ctrl·+" vs "Ctrl++"), so those
keys keep their compact symbols. U+00B7 MIDDLE DOT specifically — a Latin-1 character present
in every font, immune to the substitution that killed the arrow glyphs (the math-block dot
operator is not).
*/
[[nodiscard]] juce::String keyChordJoiner();

/*!
\brief Formats a chord for display, collapsing shifted characters.

Chords render capitalized with tight middle-dot joins — "Ctrl·Shift·Z" (see `keyChordJoiner`).
Named keys use canonical spellings ("Space", "Enter", "Esc", "Page Up", "Num 5"; arrows as
bare direction words). A chord whose shifted character differs from its base by more than
letter case renders as the character the user actually types — `Shift+/` displays as "?" —
with any remaining modifiers kept ("Ctrl·?"). Letter chords keep their explicit form
("Shift·Z"), because plain letter chords display uppercase too.

Every user-facing rendering of a chord — dialog chips, capture preview, conflict prompts, and
menu shortcut text — must route through this function so the surfaces can never drift apart.

\param key The chord to format.
\param resolve_shifted Shifted-character resolver; tests inject a fake for layout-independent
       assertions.
\return The display text for the chord.
*/
[[nodiscard]] juce::String keyChordText(
    const juce::KeyPress& key, ShiftedCharacterResolver resolve_shifted);

/*! \brief Formats a chord for display using the live keyboard layout's resolver. */
[[nodiscard]] juce::String keyChordText(const juce::KeyPress& key);

/*!
\brief Adds a command-backed menu item whose shortcut text uses `keyChordText`.

Mirrors `juce::PopupMenu::addCommandItem` (name, enablement, and tick state from the command
info; invocation through the manager) but pre-fills the item's shortcut text from this unit's
formatter — the popup only derives its own raw-`getTextDescription` text when that field
arrives empty, so this is the seam that keeps menus consistent with the dialog chips.

\param menu Menu receiving the item.
\param command_manager Manager the command is registered with; the item invokes through it.
\param command The registered command to add.
*/
void addEditorCommandItem(
    juce::PopupMenu& menu, juce::ApplicationCommandManager& command_manager,
    EditorCommandId command);

} // namespace rock_hero::editor::ui
