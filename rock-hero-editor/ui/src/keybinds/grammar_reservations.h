/*!
\file grammar_reservations.h
\brief The interaction grammar's reserved keys: capture refusal plus dialog reference rows.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief One fixed editing/navigation entry shown in the shortcuts dialog's reference section.

The verb name carries the semantics; each chip label is a plain key description styled like the
command rows' binding chips, so fixed rows and rebindable rows read the same way.
*/
struct GrammarReservation final
{
    /*! \brief Display name of the editing or navigation verb. */
    const char* name{""};

    /*! \brief Key-description chip labels, in display order. */
    std::vector<const char*> chips{};
};

/*!
\brief Returns the fixed editing/navigation rows displayed by the shortcuts dialog.

Display data only, and deliberately limited to shipped grammar: the authoritative semantics
live in the interaction model; per-surface teaching is the discovery context menus' job.

\return Reference rows in display order.
*/
[[nodiscard]] const std::vector<GrammarReservation>& grammarReservations();

/*!
\brief True when a chord's physical key belongs to the interaction grammar.

Reservation is by physical key, ignoring modifiers, because the grammar is a modifier algebra:
arrows, jumps, digits, Delete/Insert/Esc, and the `+`/`-` family compose with Ctrl (reach or
precision), Alt (author), and Shift (range) as whole families, so no modifier shape of these
keys is available for command bindings. Return is reserved ahead of its planned drill-in verb.
The capture flow refuses reserved chords so a user binding can never be shadowed by the
grammar decoder, which runs before the command mapping set and would swallow the chord
whenever its surface context applies (a sometimes-works binding, banned outright).

\param key Chord to test.
\return True when the chord's key is grammar-reserved.
*/
[[nodiscard]] bool isReservedGrammarChord(const juce::KeyPress& key);

} // namespace rock_hero::editor::ui
