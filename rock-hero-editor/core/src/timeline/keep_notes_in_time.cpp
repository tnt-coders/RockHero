#include "timeline/keep_notes_in_time.h"

#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <utility>

namespace rock_hero::editor::core
{

ContentMap timeKeptPlaces(common::core::TempoMap old_map, common::core::TempoMap new_map)
{
    return [from = std::move(old_map),
            to = std::move(new_map)](const common::core::Fraction whole_note) {
        return to.wholeNotePositionAt(nearestTempoGridPosition(
            to,
            common::core::g_tick_quantum_note_value,
            common::core::TimePosition{from.secondsAtWholeNote(whole_note)}));
    };
}

// Meters and anchors alike are the new map's: the content is only ever addressed by TIME, so the
// map it came from contributes nothing but those times.
std::expected<common::core::SongTimeline, ReaddressError> timeKeptTimeline(
    const common::core::SongTimeline& timeline, const common::core::TempoMap& tempo_map)
{
    return readdressTimeline(
        timeline,
        placedMetersOf(tempo_map),
        placedAnchorsOf(tempo_map),
        timeKeptPlaces(timeline.tempo_map, tempo_map));
}

} // namespace rock_hero::editor::core
