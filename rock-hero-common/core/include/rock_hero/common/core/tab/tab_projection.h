/*!
\file tab_projection.h
\brief Pure projection from an arrangement's chart to tablature view state.
*/

#pragma once

#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/tab/tab_view_state.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief Projects an arrangement's chart into seconds-resolved tablature view state.

Every musical position resolves through the tempo map exactly once here; the returned state is
what the tab lane renders without further grid queries. An arrangement without a chart projects
an empty state (string_count zero), which renders nothing.

\param arrangement Arrangement whose chart should be displayed.
\param tempo_map Tempo map used to resolve musical positions to seconds.
\return Render state for the tablature lane.
*/
[[nodiscard]] TabViewState makeTabViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
