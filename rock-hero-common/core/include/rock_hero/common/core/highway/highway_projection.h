/*!
\file highway_projection.h
\brief Projection from the chart domain model to the seconds-resolved highway view state.
*/

#pragma once

#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief Resolves an arrangement's chart to highway view content through the tempo map.

Mirrors the editor's tab projection discipline so both products read identical seconds for
identical inputs. Build once per chart load and share the result immutably: the camera and every
drawer are pure functions of the returned state plus per-frame time.

\param arrangement Arrangement whose loaded chart is projected; an absent chart yields an empty
       state.
\param tempo_map Tempo map resolving musical positions to seconds.
\param options Display-mapping flags (lefty mirror, string-order invert) baked into the state.
\return Seconds-resolved highway content for rendering.
*/
[[nodiscard]] HighwayViewState makeHighwayViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map, HighwayDisplayOptions options);

} // namespace rock_hero::common::core
