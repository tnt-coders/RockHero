/*!
\file chart_tokens.h
\brief Token grammar for chart grid positions and fraction-valued durations.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Parses a grid position token: `"<measure>:<beat>"` or `"<measure>:<beat>+<n>/<d>"`.

The editor's address of a place: what the readout, the prompts and every diagnostic spell, and
what a test fixture states a position in. The file stores no token; it stores the whole-note place
(\ref parseFractionToken). The fraction is a proper one in (0, 1), in the one canonical spelling.

\param text Token text.
\return Parsed position, or nullopt when the token is malformed.
*/
[[nodiscard]] std::optional<GridPosition> parseGridPositionToken(const std::string& text);

/*!
\brief Formats a grid position token, omitting the fraction for whole beats.
\param position Position to format.
\return Token text in the canonical spelling.
*/
[[nodiscard]] std::string formatGridPositionToken(const GridPosition& position);

/*!
\brief Formats a place on a string, `"<position> string <n>"`, the one spelling every message
       naming a note's slot uses.
\param position Position on the grid.
\param string One-based string, counted from the lowest-pitched string.
\return The place's text, its position in the canonical token spelling.
*/
[[nodiscard]] std::string formatChartPlaceToken(const GridPosition& position, int string);

/*!
\brief Formats a fret-hand placement, `"hand position <position>"`, the one spelling every message
       naming a placement uses.
\param position The placement's position on the grid.
\return The placement's text, its position in the canonical token spelling.
*/
[[nodiscard]] std::string formatHandPositionToken(const GridPosition& position);

/*!
\brief Formats a shape hold, `"shape hold <start> to <end>"`, the one spelling every message naming
       a hold uses.
\param hold The hold.
\return The hold's text, its edges in the canonical token spelling.
*/
[[nodiscard]] std::string formatShapeHoldToken(const ShapeHold& hold);

/*!
\brief Parses the file's one number grammar: a non-negative rational as a canonical mixed number.

The whole part, then `+<n>/<d>` for a remaining fraction in (0, 1) in lowest terms, each part
omitted when zero: `"11+3/4"`, `"12"`, `"3/8"`, `"0"`. Every value has exactly one spelling, so an
improper fraction (`"47/4"`), an unreduced one (`"2/4"`) and a written zero part (`"0+3/4"`) are
refused. One grammar serves every quantity the file states on the whole-note axis — a position's
place, a note's `sustain`, a keyframe's `offset` — and the `+<n>/<d>` beat fraction inside a grid
position token. The token names no unit; the field it fills does.

\param text Token text.
\return Parsed non-negative fraction, or nullopt when the token is not the canonical spelling.
*/
[[nodiscard]] std::optional<Fraction> parseFractionToken(const std::string& text);

/*!
\brief Formats a non-negative rational in the canonical mixed spelling parseFractionToken() reads.
\param value Fraction to format; non-negative.
\return Token text in the canonical spelling.
*/
[[nodiscard]] std::string formatFractionToken(Fraction value);

/*!
\brief Parses a stored position: a whole-note place (\ref parseFractionToken) on the tick lattice,
addressed on the grid.

THE way every `at` in a package — a note, a hand position, a section, an anchor, a tone change, an
automation point — enters memory. The grammar and the lattice are judged here, so no position
between two ticks ever exists in memory; whether the place is one the grid holds otherwise (before
the terminal, in order) is each document's rules' question, asked of the position it becomes
exactly as of any other.

\param tempo_map Grid the place is addressed on; only its meters are read.
\param text The place's text.
\return The grid position at that place, or nullopt when the text is not a canonical place on the
        tick lattice.
*/
[[nodiscard]] std::optional<GridPosition> parsePlaceToken(
    const TempoMap& tempo_map, const std::string& text);

/*!
\brief Formats a stored position as its whole-note place: the inverse of \ref parsePlaceToken, and
the way every `at` leaves memory for a package.
\param tempo_map Grid the position is addressed on.
\param position Valid grid position.
\return The place's text in the canonical spelling.
*/
[[nodiscard]] std::string formatPlaceToken(const TempoMap& tempo_map, const GridPosition& position);

} // namespace rock_hero::common::core
