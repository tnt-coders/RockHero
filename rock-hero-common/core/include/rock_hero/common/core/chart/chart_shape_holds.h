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
\brief The end a shape hold's notes bear out — the one judgement of shape rule 9
(\ref design_rulesets_shapes) that both keeping holds true after an edit and refusing a hold verb
ask.

An unnamed hold states, on each string, the pressed stop of the first fretting-hand note struck
there inside it (open strings, natural harmonics and the picking hand state nothing). Its first
CONFLICT is the earliest instant inside it where a later note releases a stated stop under
hand-position rule 6 (\ref stringReleasesStopAt). Its END CHECK fails where every stated stop is
still sounding across the end, from a note struck inside the hold that grips it; a hold stating no
pressed stop always passes.

So the answer is the first conflict's instant where one lies inside the hold, the stated end where
it passes the end check, and otherwise the earliest later end that does — judged over the stops the
longer hold states, a string first struck inside the extension included. No conflict can lie inside
that extension: a note releasing a stop there would end its sound sooner. A no-arpeggio hold states
no stop, so its stated end is the answer.

One pass over the notes inside the hold, plus those inside an extension it needs, against the
revision's per-string index (\ref ChartConnections::string_notes).

\param connections The resolved connections of the chart the hold belongs to.
\param tempo_map Tempo map the connections were resolved against, placing the hold's edges.
\param hold The hold judged; structurally valid (\ref validateShapeHolds).
\return The end on the whole-note axis: before the stated end where a conflict crops the hold,
        after it where the hold must extend, and the stated end exactly where the hold is valid.
*/
[[nodiscard]] Fraction shapeHoldNormalEnd(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold);

/*!
\brief Whether a shape hold is valid: its notes bear out exactly its stated end
(\ref shapeHoldNormalEnd).

\param connections The resolved connections of the chart the hold belongs to.
\param tempo_map Tempo map the connections were resolved against, placing the hold's edges.
\param hold The hold judged; structurally valid (\ref validateShapeHolds).
\return True when no conflict lies inside the hold and its end passes the end check.
*/
[[nodiscard]] bool shapeHoldIsValid(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold);

} // namespace rock_hero::common::core
