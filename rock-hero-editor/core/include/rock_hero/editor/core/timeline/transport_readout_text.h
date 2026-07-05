/*!
\file transport_readout_text.h
\brief Formats transport cursor positions for the editor's transport-strip readout.
*/

#pragma once

#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Formats an absolute timeline position as (h:)m:ss:mmm.

The hour field appears only once the position reaches one hour. Negative positions clamp to zero.

\param seconds Absolute timeline position in seconds.
\return Readout text such as "0:01:500" or "1:01:01:250".
*/
[[nodiscard]] std::string timelineTimeText(double seconds);

/*!
\brief Formats a transport position as measure.beat.hundredths, like REAPER's bars/beats readout.

The third field is hundredths of the way from the containing beat to the next one, so a position
halfway through measure 1 beat 2 reads "1.2.50".

\param tempo_map Song tempo map that resolves seconds to musical positions.
\param seconds Absolute timeline position in seconds.
\return Readout text such as "1.2.50".
*/
[[nodiscard]] std::string beatPositionText(const common::core::TempoMap& tempo_map, double seconds);

} // namespace rock_hero::editor::core
