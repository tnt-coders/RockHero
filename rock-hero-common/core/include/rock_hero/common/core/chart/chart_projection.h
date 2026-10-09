/*!
\file chart_projection.h
\brief The one projection from an arrangement's chart to its seconds-resolved view state.
*/

#pragma once

#include <algorithm>
#include <optional>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief Shape rule 6 (\ref design_rulesets_shapes): how far a hold's furniture is DRAWN, measured
       from the hold's start.

A hold that a head closes keeps the minimum sustain distance before that head, so consecutive
holds show the gap every other drawn element shows instead of butting exactly: the close is pulled
back to \p limit, the head's position less one margin. The trim never retreats behind the last
thing the hold STATES (\p stated), and where even that leaves nothing, the hold falls back to its
musical close — exact adjacency, the sustain rules' own precedent. With no closing head there is
no distance to keep, and the close stands. The same rule for a posture span's rails and a tapped
chord's, in whatever unit the caller measures the hold in.

\param close The musical close, from the hold's start.
\param limit The closing head's position less one margin, from the hold's start; empty where no
       head closed the hold.
\param stated The extent of the hold's last statement; the trim's floor.
\return The drawn extent: positive where the close is, and never past it.
*/
template <typename Extent>
[[nodiscard]] constexpr Extent drawnHoldExtent(
    const Extent close, const std::optional<Extent>& limit, const Extent stated)
{
    if (!limit.has_value())
    {
        return close;
    }
    const Extent trimmed = std::max(std::min(close, *limit), stated);
    return Extent{} < trimmed ? trimmed : close;
}

/*!
\brief Projects a chart into the seconds-resolved scene both surfaces draw, from its two derived
layers.

Every musical position resolves through the tempo map exactly once here, and every per-note fact
comes from the notes layer (\ref chartResolutions): the stored note with its keyframes at their
stored instants, where its ink stops (\ref chartPresentation), each note's connection claim
resolved, and each note's hold in seconds. Every hand chip and ride comes from the hand layer
(\ref fretHandResolutions). There is ONE form: a surface draws every note to its ink end, and a
reveal draws it on to its ring end — the same note, more of it. The 2D lane renders the result as
is; the 3D highway composes it and adds board-only structure (\ref makeHighwayViewState).

The layers are taken, never derived, so a caller that keeps them (the editor's memo) pays for each
once per revision. **Scored = drawn** is a property of this state, not a rule to remember: the ink
end is what both surfaces stop at and what the scorer will judge to
(\ref ChartPresentation::ink_end).

\param chart Chart to display.
\param resolutions The chart's notes layer, derived from its notes, capo and `tempo_map`.
\param hand The chart's hand layer, derived from `resolutions` and its authored positions.
\param tempo_map Tempo map used to resolve musical positions to seconds.
\return The chart's shared view state.
*/
[[nodiscard]] ChartViewState makeChartViewState(
    const Chart& chart, const ChartResolutions& resolutions, const FretHandResolutions& hand,
    const TempoMap& tempo_map);

/*!
\brief Projects an arrangement's chart into its scene, deriving both layers on the way.

For a caller that keeps no layers: the game's dev session builds each arrangement's scene once at
load, and tests build a scene from a chart in one call. An arrangement without a chart projects an
empty state (no strings named), which renders nothing.

\param arrangement Arrangement whose chart should be displayed.
\param tempo_map Tempo map used to resolve musical positions to seconds.
\return The chart's shared view state.
*/
[[nodiscard]] ChartViewState makeChartViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
