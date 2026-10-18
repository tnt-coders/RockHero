#include "song/song_timeline.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <span>
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

namespace
{

// Every slice's rules, each chart judged by `chart_rules(index, chart)`: the one place the slices
// are listed, whichever way the charts' notes layers are come by.
template <typename ChartRules>
[[nodiscard]] std::expected<void, std::string> validateTimeline(
    const SongTimeline& timeline, const ChartRules& chart_rules)
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
    for (std::size_t index = 0; index < timeline.arrangements.size(); ++index)
    {
        const ArrangementTimeline& arrangement = timeline.arrangements[index];
        const std::string prefix = "arrangement " + arrangement.id + ": ";
        if (arrangement.chart.has_value())
        {
            if (const auto valid = chart_rules(index, *arrangement.chart); !valid.has_value())
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

} // namespace

std::expected<void, std::string> validateSongTimelineRules(const SongTimeline& timeline)
{
    return validateTimeline(timeline, [&timeline](std::size_t /*index*/, const Chart& chart) {
        return validateChartRules(chart, timeline.tempo_map);
    });
}

std::expected<void, std::string> validateSongTimelineRules(
    const SongTimeline& timeline,
    const std::span<const std::optional<ChartResolutions>> notes_layers)
{
    assert(
        notes_layers.size() == timeline.arrangements.size() &&
        "a notes layer per arrangement, index-parallel");
    return validateTimeline(
        timeline, [&timeline, notes_layers](const std::size_t index, const Chart& chart) {
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<ChartResolutions>& notes_layer = notes_layers[index];
            return notes_layer.has_value()
                       ? validateChartRules(chart, timeline.tempo_map, *notes_layer)
                       : validateChartRules(chart, timeline.tempo_map);
        });
}

} // namespace rock_hero::common::core
