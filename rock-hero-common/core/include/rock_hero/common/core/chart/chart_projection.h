/*!
\file chart_projection.h
\brief The one projection from an arrangement's chart to its seconds-resolved view state.
*/

#pragma once

#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief Projects an arrangement's chart into the seconds-resolved scene both surfaces draw.

Every musical position resolves through the tempo map exactly once here, and every per-note fact
comes from the one resolutions pass (\ref chartResolutions): the SAVED stream is what is drawn (a
pick slide overrides its other techniques in memory, per chart.h, and the display must show the
scrape without them), each note's connection claim arrives resolved, and the span convention's
effective holds arrive in seconds. The 2D lane renders the result as is; the 3D highway composes
it and adds board-only structure (\ref makeHighwayViewState). An arrangement without a chart
projects an empty state (string_count zero), which renders nothing.

\param arrangement Arrangement whose chart should be displayed.
\param tempo_map Tempo map used to resolve musical positions to seconds.
\return The chart's shared view state.
*/
[[nodiscard]] ChartViewState makeChartViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
