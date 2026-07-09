#include "tone_track_projection.h"

#include <utility>

namespace rock_hero::editor::core
{

ToneTrackViewState toneTrackViewStateFor(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& selected_region_id)
{
    ToneTrackViewState state;

    // The load baseline guarantees explicit regions for every loaded arrangement, so an empty
    // track only occurs with no arrangement content and simply renders nothing.
    const common::core::ToneTrack& tone_track = arrangement.tone_track;
    state.regions.reserve(tone_track.regions.size());
    for (std::size_t index = 0; index < tone_track.regions.size(); ++index)
    {
        const common::core::ToneRegion& region = tone_track.regions[index];
        // The baseline (first) region owns the pre-measure-1 lead-in, so it extends back to the
        // timeline origin; no one-based grid position can address time before measure 1. Later
        // regions use their authored grid start.
        const double start_seconds =
            index == 0 ? 0.0 : tempo_map.secondsAtBeat(region.start.measure, region.start.beat);
        state.regions.push_back(
            ToneRegionViewState{
                .id = region.id,
                .name = toneNameFor(arrangement, region.tone_document_ref),
                .tone_document_ref = region.tone_document_ref,
                .grid_start = region.start,
                .grid_end = region.end,
                .time_range =
                    common::core::TimeRange{
                        .start = common::core::TimePosition{start_seconds},
                        .end = common::core::TimePosition{tempo_map.secondsAtBeat(
                            region.end.measure, region.end.beat)},
                    },
                .selected = !selected_region_id.empty() && region.id == selected_region_id,
            });
    }

    return state;
}

} // namespace rock_hero::editor::core
