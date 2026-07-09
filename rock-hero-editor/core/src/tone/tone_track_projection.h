/*!
\file tone_track_projection.h
\brief Pure projection from an arrangement's tone schedule to tone-track view state.
*/

#pragma once

#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Projects an arrangement's tone schedule into view state for the tone track row.

Authored regions resolve their musical endpoints to seconds through the tempo map. An arrangement
with no authored regions but a legacy tone document projects one synthesized full-length default
region (measure 1 beat 1 through the tempo-map terminal anchor); the synthesized region is
runtime-only presentation and is never written back to the song document.

\param arrangement Arrangement whose tone schedule should be displayed.
\param tempo_map Tempo map used to resolve musical endpoints to seconds.
\param active_region_id Stable id of the active (audible/edited) region; drawn with the highlight
fill.
\param selected_region_id Stable id of the formally selected region; drawn with a white outline.
Empty selects nothing.
\return Render state for the tone track row.
*/
[[nodiscard]] ToneTrackViewState toneTrackViewStateFor(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& active_region_id, const std::string& selected_region_id);

} // namespace rock_hero::editor::core
