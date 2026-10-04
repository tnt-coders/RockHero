#include "timeline/meter_edits.h"

#include <algorithm>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/grid_position.h>

namespace rock_hero::editor::core
{

// One address query per measure on each side, once per command: a song's measures, not its notes.
std::optional<int> firstRenumberedMeasure(
    const common::core::TempoMap& before, const common::core::TempoMap& after)
{
    const int last = std::max(
        common::core::terminalGridPosition(before).measure,
        common::core::terminalGridPosition(after).measure);
    for (int measure = 1; measure <= last; ++measure)
    {
        const common::core::GridPosition downbeat = common::core::measureDownbeat(measure);
        if (before.wholeNotePositionAt(downbeat) != after.wholeNotePositionAt(downbeat))
        {
            return measure;
        }
    }
    return std::nullopt;
}

// An anchor standing where it stood keeps its time.
std::expected<common::core::SongTimeline, ReaddressError> readdressUnderMeters(
    const common::core::SongTimeline& timeline, std::span<const PlacedMeter> meters)
{
    return readdressTimeline(timeline, meters, placedAnchorsOf(timeline.tempo_map), keptPlace);
}

} // namespace rock_hero::editor::core
