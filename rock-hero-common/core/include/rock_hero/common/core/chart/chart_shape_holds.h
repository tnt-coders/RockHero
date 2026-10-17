/*!
\file chart_shape_holds.h
\brief Whether the notes bear out a charter's shape hold: shape rule 9's one judgement.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*! \brief The resolved connections a hold is judged against (`chart_legato.h`). */
struct ChartConnections;

/*!
\brief The end a shape hold's notes bear out, by shape rule 9 (\ref design_rulesets_shapes): the
one judgement that keeping holds true after an edit and refusing a hold verb both ask.

\param connections The resolved connections of the chart the hold belongs to.
\param tempo_map Tempo map the connections were resolved against, placing the hold's edges.
\param hold The hold judged; structurally valid (\ref validateShapeHolds).
\return The end on the whole-note axis: the first conflict where one lies inside the hold, the
        stated end where it passes the end check, and otherwise the earliest later end that does,
        never past the song's end.
*/
[[nodiscard]] Fraction shapeHoldNormalEnd(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold);

/*!
\brief Whether a shape hold fits its notes: they bear out exactly its stated end
(\ref shapeHoldNormalEnd).

\param connections The resolved connections of the chart the hold belongs to.
\param tempo_map Tempo map the connections were resolved against, placing the hold's edges.
\param hold The hold judged; structurally valid (\ref validateShapeHolds).
\return True when the hold's normal end is its stated end.
*/
[[nodiscard]] bool shapeHoldFitsNotes(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold);

} // namespace rock_hero::common::core
