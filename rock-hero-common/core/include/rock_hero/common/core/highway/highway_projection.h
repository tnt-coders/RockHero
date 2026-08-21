/*!
\file highway_projection.h
\brief Projection from the chart domain model to the seconds-resolved highway view state.
*/

#pragma once

#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Resolves an arrangement to highway view content: the shared chart scene plus the board's
own structure.

The chart scene comes from \ref makeChartViewState, the one projection both surfaces share, so the
board and the tab lane read identical seconds for identical inputs by construction. On top of it
this derives what only the board draws: section labels, the picking-hand light onsets, the onset
groups and their repeat classification, the beat grid, and the camera framing zones. Build once
per chart load and share the result immutably: the camera and every drawer are pure functions of
the returned state plus per-frame time.

\param arrangement Arrangement whose loaded chart is projected; an absent chart yields an empty
       scene.
\param tempo_map Tempo map resolving musical positions to seconds.
\param sections Song-structure section markers, resolved into the state's section list even when
       the arrangement has no chart.
\param options Display mapping carried to the renderer: the lefty mirror, the string-order invert,
       and the minimum string count. None of them changes the projected scene — padding and
       reflection are resolved per frame.
\return Seconds-resolved highway content for rendering.
*/
[[nodiscard]] HighwayViewState makeHighwayViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map,
    const std::vector<SongSection>& sections, HighwayDisplayOptions options);

} // namespace rock_hero::common::core
