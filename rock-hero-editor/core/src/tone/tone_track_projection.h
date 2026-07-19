/*!
\file tone_track_projection.h
\brief Pure projection from an arrangement's tone schedule to tone-track view state.
*/

#pragma once

#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief One authored tone region's span in absolute seconds — the single span rule.

The baseline (first) region owns the pre-measure-1 lead-in, so it extends back to the timeline
origin; every other region resolves its sub-beat musical endpoints exactly (offsets included).
Every consumer of a region span — the tone-track projection, cursor-follow region resolution,
and the automation editable window — converts through this one helper so their notions of "the
region's span" can never diverge.

\param tempo_map Tempo map used to resolve musical endpoints to seconds.
\param region Authored region whose span is resolved.
\param is_baseline_region True for the track's first region (owns the lead-in from 0 s).
\return The region's span in absolute seconds.
*/
[[nodiscard]] common::core::TimeRange toneRegionSpanSeconds(
    const common::core::TempoMap& tempo_map, const common::core::ToneRegion& region,
    bool is_baseline_region);

/*!
\brief Projects an arrangement's tone schedule into view state for the tone track row.

Authored regions resolve their musical endpoints to seconds through the tempo map, and the first
region extends back to the timeline origin. A track with no authored regions renders nothing,
because load normalization guarantees every arrangement reaches the editor with explicit regions.

\param arrangement Arrangement whose tone schedule should be displayed.
\param tempo_map Tempo map used to resolve musical endpoints to seconds.
\param active_region_id Stable id of the active (audible/edited) region; drawn with the highlight
fill.
\param selected_region_id Stable id of the formally selected region; drawn with a white outline.
Empty selects nothing.
\return Render state for the tone track row.
*/
[[nodiscard]] ToneTrackViewState makeToneTrackViewState(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& active_region_id, const std::string& selected_region_id);

} // namespace rock_hero::editor::core
