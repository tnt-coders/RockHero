#include "tone_track_projection.h"

#include <utility>

namespace rock_hero::editor::core
{

ToneTrackViewState toneTrackViewStateFor(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& selected_region_id)
{
    ToneTrackViewState state;

    if (arrangement.tone_track.regions.empty())
    {
        if (arrangement.tone_document_ref.empty())
        {
            return state;
        }

        const auto [terminal_measure, terminal_beat] =
            tempo_map.beatAtGlobalIndex(tempo_map.terminalGlobalBeatIndex());
        state.regions.push_back(
            ToneRegionViewState{
                .id = std::string{},
                .name = toneNameFor(arrangement, arrangement.tone_document_ref),
                .grid_start = common::core::ToneGridPosition{.measure = 1, .beat = 1},
                .grid_end =
                    common::core::ToneGridPosition{
                        .measure = terminal_measure, .beat = terminal_beat
                    },
                .time_range =
                    common::core::TimeRange{
                        .start = common::core::TimePosition{tempo_map.secondsAtBeat(1, 1)},
                        .end = common::core::TimePosition{tempo_map.secondsAtBeat(
                            terminal_measure, terminal_beat)},
                    },
                .synthesized_default = true,
                .selected = false,
            });
        return state;
    }

    state.regions.reserve(arrangement.tone_track.regions.size());
    for (const common::core::ToneRegion& region : arrangement.tone_track.regions)
    {
        state.regions.push_back(
            ToneRegionViewState{
                .id = region.id,
                .name = toneNameFor(arrangement, region.tone_document_ref),
                .grid_start = region.start,
                .grid_end = region.end,
                .time_range =
                    common::core::TimeRange{
                        .start = common::core::TimePosition{tempo_map.secondsAtBeat(
                            region.start.measure, region.start.beat)},
                        .end = common::core::TimePosition{tempo_map.secondsAtBeat(
                            region.end.measure, region.end.beat)},
                    },
                .synthesized_default = false,
                .selected = !selected_region_id.empty() && region.id == selected_region_id,
            });
    }

    return state;
}

} // namespace rock_hero::editor::core
