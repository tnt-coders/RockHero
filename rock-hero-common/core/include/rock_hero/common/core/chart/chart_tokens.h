/*!
\file chart_tokens.h
\brief Token grammar for chart grid positions and beat-fraction durations.
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

The whole-beat form matches the tempo-map anchor grammar exactly; the fractional extension
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
\brief Parses a beat-fraction token: `"<n>"` for whole beats or `"<n>/<d>"` for fractions.
\param text Token text.
\return Parsed non-negative fraction, or nullopt when the token is malformed.
*/
[[nodiscard]] std::optional<Fraction> parseBeatFractionToken(const std::string& text);

/*!
\brief Formats a beat-fraction token, omitting the denominator for whole values.
\param value Fraction to format.
\return Token text in the canonical spelling.
*/
[[nodiscard]] std::string formatBeatFractionToken(Fraction value);

} // namespace rock_hero::common::core
