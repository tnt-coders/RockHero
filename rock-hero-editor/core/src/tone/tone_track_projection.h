/*!
\file tone_track_projection.h
\brief Pure projection from an arrangement's tone schedule to tone-track view state.
*/

#pragma once

#include <cstddef>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Where a tone region ends: the next region's start, or the terminal anchor for the last.

A region stores only its start, so its end is this one derivation; the projection, the span rule
below and nothing else spell it.

\param tone_track Track holding the region.
\param index Index of the region on the track; must be in range.
\param tempo_map Tempo map whose terminal anchor closes the last region.
\return The region's exclusive end.
*/
[[nodiscard]] common::core::GridPosition toneRegionEnd(
    const common::core::ToneTrack& tone_track, std::size_t index,
    const common::core::TempoMap& tempo_map);

/*!
\brief One authored tone region's span in absolute seconds — the single span rule.

The baseline (first) region owns the pre-measure-1 lead-in, so it extends back to the timeline
origin; every other region resolves its sub-beat musical start exactly (offset included), and
every region ends where \ref toneRegionEnd says. Every consumer of a region span — the tone-track
projection, cursor-follow region resolution, and the automation editable window — converts
through this one helper so their notions of "the region's span" can never diverge.

\param tempo_map Tempo map used to resolve musical endpoints to seconds.
\param tone_track Track holding the region.
\param index Index of the region on the track; must be in range.
\return The region's span in absolute seconds.
*/
[[nodiscard]] common::core::TimeRange toneRegionSpanSeconds(
    const common::core::TempoMap& tempo_map, const common::core::ToneTrack& tone_track,
    std::size_t index);

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
