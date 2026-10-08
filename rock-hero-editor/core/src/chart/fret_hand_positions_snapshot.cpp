#include "chart/fret_hand_positions_snapshot.h"

#include <algorithm>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::editor::core
{

// An empty snapshot for a session with no chart, rather than a failure: every hand verb then finds
// no placement to edit and never reaches a commit.
FretHandPositionsSnapshot FretHandPositionsSnapshot::capture(const common::core::Session& session)
{
    const common::core::Arrangement* const arrangement = session.currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return {};
    }
    return FretHandPositionsSnapshot{.placements = arrangement->chart->fret_hand_positions};
}

// Reports failure rather than silently doing nothing when no chart owns a stream, which is what
// lets an undo of a hand edit fault the transition instead of appearing to succeed. The write goes
// through the session's position surface, which advances the chart revision the tab and highway
// projections are keyed on, so no cached projection can draw the stream it replaced, and leaves
// the notes revision, so the notes layer derived before the edit stands.
bool FretHandPositionsSnapshot::applyTo(common::core::Session& session) const
{
    return session.writeFretHandPositions(placements);
}

// A sort rather than a sorted insert, so an insert and a move can each just place their placement
// and let the one normalization put the stream back in the order every consumer searches it in.
void FretHandPositionsSnapshot::normalize()
{
    std::ranges::sort(placements, std::ranges::less{}, &common::core::FretHandPosition::position);
}

// Every placement rule lives in common core, where the package reader asks the same questions of
// the same stream, so this only carries the diagnostic across: the stream's own rules, then its
// coverage of the chart's notes, read from the notes layer the context carries rather than derived
// again. With no chart there is nothing to validate against and nothing to apply onto; the apply
// reports that.
std::optional<std::string> FretHandPositionsSnapshot::validate(
    const MarkerJudgeContext& context) const
{
    const common::core::Arrangement* const arrangement = context.session.currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::TempoMap& tempo_map = context.session.song().tempo_map;
    if (const auto valid =
            common::core::validateFretHandPositions(placements, chart.tuning, tempo_map);
        !valid.has_value())
    {
        return valid.error().message;
    }
    if (const auto covered =
            common::core::validateFretHandCoverage(context.chart, placements, tempo_map);
        !covered.has_value())
    {
        return covered.error().message;
    }
    return std::nullopt;
}

EditFocus FretHandPositionsSnapshot::focusReplacing(const FretHandPositionsSnapshot& replaced) const
{
    const std::optional<ChangedRecord<common::core::FretHandPosition>> changed =
        firstChangedRecord(placements, replaced.placements);
    if (!changed.has_value())
    {
        return {};
    }
    const common::core::GridPosition start = changed->record->position;
    MarkerEditFocus marker{.marker = {}, .start = start};
    if (changed->present)
    {
        marker.marker = FretHandPositionSelection{.position = start};
    }
    return marker;
}

} // namespace rock_hero::editor::core
