#include "tone_track_projection.h"

#include <utility>

namespace rock_hero::editor::core
{

common::core::TimeRange toneRegionSpanSeconds(
    const common::core::TempoMap& tempo_map, const common::core::ToneRegion& region,
    bool is_baseline_region)
{
    // The baseline (first) region owns the pre-measure-1 lead-in, so it extends back to the
    // timeline origin; no one-based grid position can address time before measure 1. Later
    // regions use their authored grid start. Endpoints resolve sub-beat exactly — dropping the
    // offset here once made cursor-follow disagree with the drawn spans on off-beat boundaries.
    const double start_seconds =
        is_baseline_region
            ? 0.0
            : tempo_map.secondsAtNote(region.start.measure, region.start.beat, region.start.offset);
    return common::core::TimeRange{
        .start = common::core::TimePosition{start_seconds},
        .end = common::core::TimePosition{tempo_map.secondsAtNote(
            region.end.measure, region.end.beat, region.end.offset)},
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
                .grid_end = region.end,
                .time_range = toneRegionSpanSeconds(tempo_map, region, index == 0),
                .active = !active_region_id.empty() && region.id == active_region_id,
                .selected = !selected_region_id.empty() && region.id == selected_region_id,
            });
    }

    return state;
}

} // namespace rock_hero::editor::core
