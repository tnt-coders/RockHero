#include "timeline/section_projection.h"

#include <rock_hero/common/core/chart/grid_arithmetic.h>

namespace rock_hero::editor::core
{

std::vector<SongSectionViewState> makeSongSectionViews(
    const std::vector<common::core::SongSection>& sections, const common::core::TempoMap& tempo_map,
    const std::optional<common::core::GridPosition> selected_position)
{
    std::vector<SongSectionViewState> views;
    views.reserve(sections.size());
    for (const common::core::SongSection& section : sections)
    {
        views.push_back(
            SongSectionViewState{
                .seconds = tempo_map.secondsAtGlobalBeatPosition(
                    common::core::globalBeatPosition(tempo_map, section.position)),
                .position = section.position,
                .name = section.name,
                // Optional-to-value comparison rather than a guarded dereference: an empty
                // optional compares unequal to every position, which is exactly "none selected".
                .selected = selected_position == section.position,
            });
    }

    return views;
}

} // namespace rock_hero::editor::core
