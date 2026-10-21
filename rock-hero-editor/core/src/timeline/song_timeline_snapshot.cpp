#include "timeline/song_timeline_snapshot.h"

#include "timeline/tempo_map_snapshot.h"
#include "tone/tone_model_snapshot.h"

#include <iterator>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/shared/overloaded.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <utility>
#include <variant>
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

// Each chart normalized under the map it now stands on, as the chart gate normalizes one after a
// note or hold edit: the holds first, then the drop of every authored position the landed layer no
// longer covers, since a position is downstream of the grid. Both ride this timeline, so the
// entry's undo restores them, and each conversion is reported after the holds' rewrites. The notes
// layer is derived once, for the memo the landing seeds.
MarkerModelNormalization SongTimelineSnapshot::normalize()
{
    MarkerModelNormalization normalization;
    normalization.notes_layers.reserve(timeline.arrangements.size());
    for (common::core::ArrangementTimeline& arrangement : timeline.arrangements)
    {
        if (!arrangement.chart.has_value())
        {
            normalization.notes_layers.emplace_back();
            continue;
        }
        common::core::Chart& chart = *arrangement.chart;
        common::core::ShapeHoldNormalization normal = common::core::normalizeShapeHolds(
            chart.notes, chart.shape_holds, chart.tuning.capo, timeline.tempo_map);
        chart.shape_holds = normal.resolved.shape_holds;
        std::vector<common::core::ChartConversion> dropped =
            common::core::dropUncoveringFretHandPositions(
                normal.resolved, chart.fret_hand_positions, timeline.tempo_map);
        normal.conversions.insert(
            normal.conversions.end(),
            std::make_move_iterator(dropped.begin()),
            std::make_move_iterator(dropped.end()));
        normalization.notes_layers.emplace_back(std::move(normal.resolved));
        for (common::core::ChartConversion& conversion : normal.conversions)
        {
            conversion.where = "arrangement " + arrangement.id + ", " + conversion.where;
            normalization.conversions.push_back(std::move(conversion));
        }
    }
    return normalization;
}

// Every rule lives in common core, where the package reader asks the same questions, so this only
// carries the first diagnostic across for the funnel's refusal log, with the culprits the breaking
// slice's records name. The culprits are lit on what the screen shows, which is the session's map:
// the one this timeline would replace.
std::optional<PlanRefused> SongTimelineSnapshot::validate(const MarkerJudgeContext& context) const
{
    const auto valid = common::core::validateSongTimelineRules(timeline);
    if (valid.has_value())
    {
        return std::nullopt;
    }
    const common::core::TempoMap& standing = context.session.song().tempo_map;
    return PlanRefused{
        .reason = common::core::songTimelineErrorText(valid.error()),
        .lit = std::visit(
            common::core::Overloaded{
                [&standing](const common::core::TempoMapError& tempo) {
                    return tempoMapCulprits(tempo, standing);
                },
                // A section rule names no record; the verb's subject is what lights.
                [](const common::core::SongSectionError& /*sections*/) { return RefusalLight{}; },
                // Only a tone track's rules name records; a chart rule or an automation entry's
                // names none.
                [](const common::core::ArrangementRuleError& arrangement) {
                    const auto* const tone =
                        std::get_if<common::core::ToneTrackError>(&arrangement.cause);
                    return tone != nullptr ? toneTrackCulprits(*tone) : RefusalLight{};
                },
            },
            valid.error()),
    };
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
