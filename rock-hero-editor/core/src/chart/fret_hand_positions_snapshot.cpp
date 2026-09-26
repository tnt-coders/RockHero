#include "chart/fret_hand_positions_snapshot.h"

#include <algorithm>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/arrangement.h>

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
// through the session's one mutable chart accessor, which advances the chart revision the tab and
// highway projections are keyed on, so no cached projection can draw the stream it replaced.
bool FretHandPositionsSnapshot::applyTo(common::core::Session& session) const
{
    common::core::Chart* const chart = session.currentChart();
    if (chart == nullptr)
    {
        return false;
    }
    chart->fret_hand_positions = placements;
    return true;
}

// A sort rather than a sorted insert, so an insert and a move can each just place their placement
// and let the one normalization put the stream back in the order every consumer searches it in.
void FretHandPositionsSnapshot::normalize()
{
    std::ranges::sort(placements, std::ranges::less{}, &common::core::FretHandPosition::position);
}

// Every placement rule lives in common core, where the package reader asks the same question of the
// same stream, so this only carries the diagnostic across. With no chart there is nothing to
// validate against and nothing to apply onto; the apply reports that.
std::optional<std::string> FretHandPositionsSnapshot::validate(
    const common::core::Session& session) const
{
    const common::core::Arrangement* const arrangement = session.currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    if (const auto valid = common::core::validateFretHandPositions(
            placements, arrangement->chart->tuning, session.song().tempo_map);
        !valid.has_value())
    {
        return valid.error().message;
    }
    return std::nullopt;
}

} // namespace rock_hero::editor::core
