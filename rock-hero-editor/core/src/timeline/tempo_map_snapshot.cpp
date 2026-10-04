#include "timeline/tempo_map_snapshot.h"

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
// this only carries the diagnostic across for the funnel's refusal log.
std::optional<std::string> TempoMapSnapshot::validate(
    const common::core::Session& /*session*/) const
{
    if (const auto valid = common::core::validateTempoMapRules(tempo_map); !valid.has_value())
    {
        return valid.error().message;
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

} // namespace rock_hero::editor::core
