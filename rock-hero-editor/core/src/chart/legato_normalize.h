/*!
\file legato_normalize.h
\brief Value-based legato repair shared by the editor planners and the importer.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Repairs impossible hammer-on / pull-off states across a sorted note stream, in place.

Normalizes impossibility, never preference: a `Pull` whose justification the stream no longer
supports becomes the hammer-on a lower released predecessor justifies, else a plain pick; a
`Hammer` with nothing to strike becomes the pull-off a higher predecessor justifies, else a plain
pick. Every other `Hammer` is untouched — always possible as a left-hand tap, so a deliberate
`Ctrl+H` survives every later edit. Judgments are value-based against the RELEASED fret (a glide
hands over its last waypoint, a scrape its slide-out's end), never predecessor identity, and a
valid pull-from-a-scrape is deliberately left standing.

Justification also requires the predecessor still holdable at the note's onset
(`predecessorHoldReaches`): past the kept-sustain bound a released predecessor supports neither
direction, so a Pull it once justified repairs to a plain pick — which is why a sustain edit
that disconnects a tail repairs its dependent legato in the same undo entry.

Runs inside the planners' shared finalize step so every edit repairs what it disturbed in the
same undo entry, and at import completion so a chart is never invalid in the first place.

\param notes Note stream sorted by (position, string); repaired in place.
\param shapes Hand-posture spans the notes play under; a span implies its strum is held, so the
hold test judges span-extended lengths (chartEffectiveSustains).
\param tempo_map Tempo map supplying the beat axis for the hold test.
*/
void normalizeChartLegato(
    std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::ChartShape>& shapes, const common::core::TempoMap& tempo_map);

} // namespace rock_hero::editor::core
