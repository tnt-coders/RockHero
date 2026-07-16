#include "timeline/section_projection.h"

namespace rock_hero::editor::core
{

namespace
{

// Converts a grid position onto the tempo map's fractional global beat axis.
[[nodiscard]] double globalBeatPosition(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    return static_cast<double>(tempo_map.globalBeatIndex(position.measure, position.beat)) +
           position.offset.toDouble();
}

} // namespace

std::vector<SongSectionView> makeSongSectionViews(
    const std::vector<common::core::SongSection>& sections, const common::core::TempoMap& tempo_map)
{
    std::vector<SongSectionView> views;
    views.reserve(sections.size());
    for (const common::core::SongSection& section : sections)
    {
        views.push_back(
            SongSectionView{
                .seconds = tempo_map.secondsAtGlobalBeatPosition(
                    globalBeatPosition(tempo_map, section.position)),
                .name = section.name,
            });
    }

    return views;
}

} // namespace rock_hero::editor::core
