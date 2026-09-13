#include "tone_track_projection.h"

#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <utility>

namespace rock_hero::editor::core
{

common::core::GridPosition toneRegionEnd(
    const common::core::ToneTrack& tone_track, const std::size_t index,
    const common::core::TempoMap& tempo_map)
{
    return index + 1 < tone_track.regions.size() ? tone_track.regions[index + 1].start
                                                 : common::core::terminalGridPosition(tempo_map);
}

common::core::TimeRange toneRegionSpanSeconds(
    const common::core::TempoMap& tempo_map, const common::core::ToneTrack& tone_track,
    const std::size_t index)
{
    // The baseline (first) region owns the pre-measure-1 lead-in, so it extends back to the
    // timeline origin; no one-based grid position can address time before measure 1. Later
    // regions use their authored grid start. Endpoints resolve sub-beat exactly — dropping the
    // offset here once made cursor-follow disagree with the drawn spans on off-beat boundaries.
    const common::core::GridPosition start = tone_track.regions[index].start;
    const common::core::GridPosition end = toneRegionEnd(tone_track, index, tempo_map);
    return common::core::TimeRange{
        .start =
            common::core::TimePosition{
                index == 0 ? 0.0 : tempo_map.secondsAtNote(start.measure, start.beat, start.offset)
            },
        .end =
            common::core::TimePosition{tempo_map.secondsAtNote(end.measure, end.beat, end.offset)},
    };
}

ToneTrackViewState makeToneTrackViewState(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& active_region_id, const std::string& selected_region_id)
{
    ToneTrackViewState state;

    // The load baseline guarantees explicit regions for every loaded arrangement, so an empty
    // track only occurs with no arrangement content and simply renders nothing.
    const common::core::ToneTrack& tone_track = arrangement.tone_track;
    state.regions.reserve(tone_track.regions.size());
    for (std::size_t index = 0; index < tone_track.regions.size(); ++index)
    {
        const common::core::ToneRegion& region = tone_track.regions[index];
        state.regions.push_back(
            ToneRegionViewState{
                .id = region.id,
                .name = toneNameFor(arrangement, region.tone_document_ref),
                .tone_document_ref = region.tone_document_ref,
                .grid_start = region.start,
                .grid_end = toneRegionEnd(tone_track, index, tempo_map),
                .time_range = toneRegionSpanSeconds(tempo_map, tone_track, index),
                .active = !active_region_id.empty() && region.id == active_region_id,
                .selected = !selected_region_id.empty() && region.id == selected_region_id,
            });
    }

    return state;
}

} // namespace rock_hero::editor::core
