#include "timeline/song_timeline_snapshot.h"

#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

SongTimelineSnapshot SongTimelineSnapshot::capture(const common::core::Session& session)
{
    return SongTimelineSnapshot{.timeline = common::core::makeSongTimeline(session.song())};
}

bool SongTimelineSnapshot::applyTo(common::core::Session& session) const
{
    return session.setSongTimeline(timeline);
}

// Each chart judged under the map it now stands on; a chart with no holds costs nothing.
std::vector<common::core::ChartConversion> SongTimelineSnapshot::normalize()
{
    std::vector<common::core::ChartConversion> rewrites;
    for (common::core::ArrangementTimeline& arrangement : timeline.arrangements)
    {
        if (!arrangement.chart.has_value())
        {
            continue;
        }
        common::core::Chart& chart = *arrangement.chart;
        common::core::ShapeHoldNormalization normal =
            common::core::normalizeShapeHolds(chart.notes, chart.shape_holds, timeline.tempo_map);
        chart.shape_holds = std::move(normal.shape_holds);
        for (common::core::ChartConversion& rewrite : normal.conversions)
        {
            rewrite.where = "arrangement " + arrangement.id + ", " + rewrite.where;
            rewrites.push_back(std::move(rewrite));
        }
    }
    return rewrites;
}

// Every rule lives in common core, where the package reader asks the same questions, so this only
// carries the first diagnostic across for the funnel's refusal log.
std::optional<std::string> SongTimelineSnapshot::validate(
    const MarkerJudgeContext& /*context*/) const
{
    if (const auto valid = common::core::validateSongTimelineRules(timeline); !valid.has_value())
    {
        return valid.error();
    }
    return std::nullopt;
}

// A meter is placed on the whole-note axis, so only the change the transition made differs; the
// focus goes where that change stands, or stood, under the landed map.
EditFocus SongTimelineSnapshot::focusReplacing(const SongTimelineSnapshot& replaced) const
{
    const std::optional<ChangedRecord<common::core::TimeSignatureChange>> changed =
        firstChangedRecord(
            timeline.tempo_map.timeSignatures(), replaced.timeline.tempo_map.timeSignatures());
    if (!changed.has_value())
    {
        return {};
    }
    const common::core::GridPosition start =
        timeline.tempo_map.gridPositionAtWholeNote(changed->record->place);
    MarkerEditFocus marker{.marker = {}, .start = start};
    if (changed->present)
    {
        marker.marker = TimeSignatureSelection{.measure = start.measure};
    }
    return marker;
}

} // namespace rock_hero::editor::core
