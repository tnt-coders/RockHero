#include "song/song_timeline.h"

#include <algorithm>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/shared/overloaded.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>
#include <utility>
#include <variant>

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

// Every slice's own validator, in the order a reader meets them. A chart is asked its structural
// rules only: coverage is the timeline normalization's drop, never a refusal of the grid edit.
std::expected<void, SongTimelineError> validateSongTimelineRules(const SongTimeline& timeline)
{
    const TempoMap& tempo_map = timeline.tempo_map;
    if (const auto valid = validateTempoMapRules(tempo_map); !valid.has_value())
    {
        return std::unexpected{valid.error()};
    }
    if (const auto valid = validateSongSectionRules(timeline.sections, tempo_map);
        !valid.has_value())
    {
        return std::unexpected{valid.error()};
    }
    for (const ArrangementTimeline& arrangement : timeline.arrangements)
    {
        if (arrangement.chart.has_value())
        {
            if (const auto valid = validateChartStructure(*arrangement.chart, tempo_map);
                !valid.has_value())
            {
                return std::unexpected{ArrangementRuleError{
                    .arrangement_id = arrangement.id,
                    .cause = valid.error(),
                }};
            }
        }
        if (const auto valid = validateToneTrackRules(arrangement.tone_track, tempo_map);
            !valid.has_value())
        {
            return std::unexpected{ArrangementRuleError{
                .arrangement_id = arrangement.id,
                .cause = valid.error(),
            }};
        }
        for (const ToneParameterAutomation& automation : arrangement.tone_automation)
        {
            if (!isValidToneParameterAutomation(automation, tempo_map))
            {
                return std::unexpected{ArrangementRuleError{
                    .arrangement_id = arrangement.id,
                    .cause = InvalidToneAutomationEntry{
                        .plugin_id = automation.plugin_id,
                    },
                }};
            }
        }
    }
    return {};
}

ArrangementRuleErrorCode ArrangementRuleError::code() const
{
    return std::visit(
        Overloaded{
            [](const ChartError& /*chart*/) { return ArrangementRuleErrorCode::ChartRule; },
            [](const ToneTrackError& /*tone_track*/) {
                return ArrangementRuleErrorCode::ToneTrackRule;
            },
            [](const InvalidToneAutomationEntry& /*automation*/) {
                return ArrangementRuleErrorCode::ToneAutomationRule;
            },
        },
        cause);
}

std::string songTimelineErrorText(const SongTimelineError& error)
{
    // An arrangement's cause keeps its own diagnostic, prefixed with the arrangement here.
    const auto arrangement_text = [](const ArrangementRuleError& arrangement) {
        const std::string cause_text = std::visit(
            Overloaded{
                [](const ChartError& chart) { return chart.message; },
                [](const ToneTrackError& tone_track) { return tone_track.message; },
                [](const InvalidToneAutomationEntry& automation) {
                    return "tone automation entry is invalid for plugin: " + automation.plugin_id;
                },
            },
            arrangement.cause);
        return "arrangement " + arrangement.arrangement_id + ": " + cause_text;
    };
    return std::visit(
        Overloaded{
            [](const TempoMapError& tempo) { return tempo.message; },
            [](const SongSectionError& section) { return section.message; },
            arrangement_text,
        },
        error);
}

} // namespace rock_hero::common::core
