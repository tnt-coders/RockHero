#include "timeline/tempo_map_snapshot.h"

#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <vector>

namespace rock_hero::editor::core
{

TempoMapSnapshot TempoMapSnapshot::capture(const common::core::Session& session)
{
    return TempoMapSnapshot{.tempo_map = session.song().tempo_map};
}

bool TempoMapSnapshot::applyTo(common::core::Session& session) const
{
    session.setTempoMap(tempo_map);
    return true;
}

void TempoMapSnapshot::normalize()
{}

// Every tempo-map rule lives in common core, where the package reader asks the same question, so
// this only carries the diagnostic across for the funnel's refusal log, with the culprits its
// records name.
std::optional<PlanRefused> TempoMapSnapshot::validate(const MarkerJudgeContext& context) const
{
    if (const auto valid = common::core::validateTempoMapRules(tempo_map); !valid.has_value())
    {
        return PlanRefused{
            .reason = valid.error().message,
            .lit = tempoMapCulprits(valid.error(), context.session.song().tempo_map),
        };
    }
    return std::nullopt;
}

EditFocus TempoMapSnapshot::focusReplacing(const TempoMapSnapshot& replaced) const
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    const std::optional<ChangedRecord<common::core::BeatAnchor>> changed =
        firstChangedRecord(anchors, replaced.tempo_map.anchors());
    if (!changed.has_value())
    {
        return {};
    }

    const common::core::BeatAnchor* focused = changed->record;
    if (changed->present && focused == &anchors.back() && anchors.size() >= 2)
    {
        focused = &anchors[anchors.size() - 2];
    }
    const common::core::GridPosition start = focused->position;
    MarkerEditFocus marker{.marker = {}, .start = start};
    if (changed->present)
    {
        marker.marker = TempoAnchorSelection{.position = start};
    }
    return marker;
}

// An anchor is drawn at its position, which a tempo edit never changes, so the error's positions
// name chips on either map; a time signature is drawn at the first measure of its reign, which a
// meter edit renumbers, so its place is read under the map the chips are drawn by.
RefusalLight tempoMapCulprits(
    const common::core::TempoMapError& error, const common::core::TempoMap& standing)
{
    RefusalLight lit;
    for (const common::core::GridPosition& position : error.anchor_positions)
    {
        lit.add(TempoAnchorSelection{.position = position});
    }
    for (const common::core::Fraction& place : error.meter_places)
    {
        lit.add(TimeSignatureSelection{.measure = standing.gridPositionAtWholeNote(place).measure});
    }
    return lit;
}

} // namespace rock_hero::editor::core
