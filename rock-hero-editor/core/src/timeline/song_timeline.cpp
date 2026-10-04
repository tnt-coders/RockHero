#include "timeline/song_timeline.h"

#include <cassert>
#include <cstddef>
#include <utility>

namespace rock_hero::editor::core
{

SongTimeline makeSongTimeline(const common::core::Song& song)
{
    SongTimeline timeline{
        .tempo_map = song.tempo_map,
        .sections = song.sections,
        .arrangements = {},
    };
    timeline.arrangements.reserve(song.arrangements.size());
    for (const common::core::Arrangement& arrangement : song.arrangements)
    {
        timeline.arrangements.push_back(
            ArrangementTimeline{
                .id = arrangement.id,
                .chart = arrangement.chart,
                .tone_track = arrangement.tone_track,
                .tone_automation = arrangement.tone_automation,
            });
    }
    return timeline;
}

// The shares are in song order, so the write is a walk in step; the id is asserted rather than
// searched because a timeline read from another song is a caller's defect, not a case.
common::core::Song songWithTimeline(common::core::Song song, SongTimeline timeline)
{
    assert(
        song.arrangements.size() == timeline.arrangements.size() &&
        "a timeline is written back only over the song it was read from");
    song.tempo_map = std::move(timeline.tempo_map);
    song.sections = std::move(timeline.sections);
    for (std::size_t index = 0; index < song.arrangements.size(); ++index)
    {
        common::core::Arrangement& arrangement = song.arrangements[index];
        ArrangementTimeline& share = timeline.arrangements[index];
        assert(arrangement.id == share.id && "a timeline's shares follow the song's order");
        arrangement.chart = std::move(share.chart);
        arrangement.tone_track = std::move(share.tone_track);
        arrangement.tone_automation = std::move(share.tone_automation);
    }
    return song;
}

} // namespace rock_hero::editor::core
