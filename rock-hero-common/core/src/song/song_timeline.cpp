#include "song/song_timeline.h"

#include <algorithm>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>
#include <utility>

namespace rock_hero::common::core
{

SongTimeline makeSongTimeline(const Song& song)
{
    SongTimeline timeline{
        .tempo_map = song.tempo_map,
        .sections = song.sections,
        .arrangements = {},
    };
    timeline.arrangements.reserve(song.arrangements.size());
    for (const Arrangement& arrangement : song.arrangements)
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

// The arrangements are matched by id in order before anything is written, so the write after the
// check is a walk in step.
bool writeSongTimeline(Song& song, SongTimeline timeline)
{
    if (!std::ranges::equal(
            song.arrangements,
            timeline.arrangements,
            {},
            &Arrangement::id,
            &ArrangementTimeline::id))
    {
        return false;
    }
    song.tempo_map = std::move(timeline.tempo_map);
    song.sections = std::move(timeline.sections);
    for (std::size_t index = 0; index < song.arrangements.size(); ++index)
    {
        Arrangement& arrangement = song.arrangements[index];
        ArrangementTimeline& share = timeline.arrangements[index];
        arrangement.chart = std::move(share.chart);
        arrangement.tone_track = std::move(share.tone_track);
        arrangement.tone_automation = std::move(share.tone_automation);
    }
    return true;
}

std::expected<void, std::string> validateSongTimelineRules(const SongTimeline& timeline)
{
    const TempoMap& tempo_map = timeline.tempo_map;
    if (const auto valid = validateTempoMapRules(tempo_map); !valid.has_value())
    {
        return std::unexpected{valid.error().message};
    }
    if (const auto valid = validateSongSectionRules(timeline.sections, tempo_map);
        !valid.has_value())
    {
        return std::unexpected{valid.error().message};
    }
    for (const ArrangementTimeline& arrangement : timeline.arrangements)
    {
        const std::string prefix = "arrangement " + arrangement.id + ": ";
        if (arrangement.chart.has_value())
        {
            if (const auto valid = validateChartRules(*arrangement.chart, tempo_map);
                !valid.has_value())
            {
                return std::unexpected{prefix + valid.error().message};
            }
        }
        if (const auto valid = validateToneTrackRules(arrangement.tone_track, tempo_map);
            !valid.has_value())
        {
            return std::unexpected{prefix + valid.error().message};
        }
        for (const ToneParameterAutomation& automation : arrangement.tone_automation)
        {
            if (!isValidToneParameterAutomation(automation, tempo_map))
            {
                return std::unexpected{
                    prefix + "tone automation entry is invalid for plugin: " + automation.plugin_id
                };
            }
        }
    }
    return {};
}

} // namespace rock_hero::common::core
