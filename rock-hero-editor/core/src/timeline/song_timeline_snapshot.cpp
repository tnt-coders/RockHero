#include "timeline/song_timeline_snapshot.h"

#include <algorithm>
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

// A redundant meter changes no measure's length, so dropping it moves nothing on the grid and
// every position the timeline holds keeps its token.
void SongTimelineSnapshot::normalize()
{
    std::vector<common::core::TimeSignatureChange> kept = timeline.tempo_map.timeSignatures();
    const auto redundant = std::ranges::unique(kept, {}, &common::core::TimeSignatureChange::meter);
    if (redundant.empty())
    {
        return;
    }
    kept.erase(redundant.begin(), redundant.end());
    timeline.tempo_map = common::core::TempoMap{std::move(kept), timeline.tempo_map.anchors()};
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
