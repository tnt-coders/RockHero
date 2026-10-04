/*!
\file chart_tokens.h
\brief Token grammar for chart grid positions and fraction-valued durations.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Parses a grid position token: `"<measure>:<beat>"` or `"<measure>:<beat>+<n>/<d>"`.

Every stored position and every tempo anchor is written in this grammar; the fractional form
addresses exact sub-beat positions. Fractions must be proper (in (0, 1)) and reduced values are
accepted in any equivalent spelling.

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
\brief Parses a fraction token: `"<n>"` for a whole value or `"<n>/<d>"` for a fraction.

One grammar serves two quantities: the `+<n>/<d>` beat fraction inside a grid position token, and
a duration (a note's `sustain` or a keyframe's `offset`). The token names no unit; the field it
fills does.

\param text Token text.
\return Parsed non-negative fraction, or nullopt when the token is malformed.
*/
[[nodiscard]] std::optional<Fraction> parseFractionToken(const std::string& text);

/*!
\brief Formats a fraction token, omitting the denominator for whole values.

The one spelling of both a grid position token's beat fraction and a duration; see
parseFractionToken().

\param value Fraction to format.
\return Token text in the canonical spelling.
*/
[[nodiscard]] std::string formatFractionToken(Fraction value);

} // namespace rock_hero::common::core
