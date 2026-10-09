#include "timeline/song_timeline_snapshot.h"

#include <algorithm>
#include <iterator>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Whether a stored change is redundant: dropping it would leave every measure's length as it is.
// That is so exactly when it repeats the meter of the authored reign before it from one of that
// reign's own downbeats, which the reign table shows as no derived short bar between the two. A
// repeat off that grid cuts a bar, and a meter equal to the derived bar before it stores what that
// bar only derived; both stay.
[[nodiscard]] bool repeatsItsReign(
    const std::vector<common::core::MeterReign>& reigns,
    const common::core::TimeSignatureChange& change)
{
    const auto opened = std::ranges::find(reigns, change.place, &common::core::MeterReign::place);
    if (opened == reigns.begin() || opened == reigns.end())
    {
        return false;
    }
    const common::core::MeterReign& before = *std::prev(opened);
    return !before.derived && before.meter == change.meter;
}

} // namespace

SongTimelineSnapshot SongTimelineSnapshot::capture(const common::core::Session& session)
{
    return SongTimelineSnapshot{.timeline = common::core::makeSongTimeline(session.song())};
}

bool SongTimelineSnapshot::applyTo(common::core::Session& session) const
{
    return session.setSongTimeline(timeline);
}

// Dropping a redundant change joins two reigns of one meter at a measure boundary, so no measure
// moves and every position the timeline holds keeps its token. The reign a dropped change opened
// is the same grid its predecessor's was, so the next change is judged alike against either, and
// one pass over the table settles every change.
void SongTimelineSnapshot::normalize()
{
    const std::vector<common::core::MeterReign> reigns = timeline.tempo_map.reigns();
    std::vector<common::core::TimeSignatureChange> kept;
    std::ranges::remove_copy_if(
        timeline.tempo_map.timeSignatures(),
        std::back_inserter(kept),
        [&reigns](const common::core::TimeSignatureChange& change) {
            return repeatsItsReign(reigns, change);
        });
    if (kept.size() == timeline.tempo_map.timeSignatures().size())
    {
        return;
    }
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
